#include <Arduino.h>
#include <math.h>
#include <FFat.h>
#include "JY901_Serial.h"
#include "imu_sensor.h"
#include "app_state.h"
#include "config.h"
#include "storage_services.h"
#include "diagnostic_log.h"

extern bool simulationMode;

static unsigned long gLastImuSampleMs = 0;

// -----------------------------------------------------------------------------
// JY901 - AZIMUT POR GIROSCOPIO 3D CORREGIDO
//
// El magnetometro y YAWraw quedan completamente fuera del control de AZ.
//
// 1) En reposo se aprende el offset de GX/GY/GZ.
// 2) Durante movimiento:
//      rate = |gyro - offset|
// 3) El signo NO se toma del sensor. Lo da la orden del motor:
//      +1 derecha
//      -1 izquierda
// 4) angleZ integra rate * dt.
// 5) En reposo angleZ queda congelado.
//
// Esto hace la medida independiente de:
// - correcciones del magnetometro,
// - cambios de YAWraw,
// - inclinacion moderada del JY901 respecto al eje mecanico.
//
// El vector bruto puede cambiar de eje, pero su magnitud corregida conserva
// la velocidad angular.
//
// Sin SAVE.
// -----------------------------------------------------------------------------

static constexpr uint16_t kWzStaticMoving = 20;   // 0.020 deg/s
static constexpr uint16_t kWzStaticIdle   = 650;  // 0.650 deg/s
static constexpr uint16_t kWzTimeIdleMs   = 2000;

// Ruido residual después de restar bias.
static constexpr float kGyroNoiseFloorDps = 0.10f;

// Protección frente a golpes / paquetes espurios.
// El rotor real está muy por debajo de este valor.
static constexpr float kMaxRotorRateDps = 3.00f;

// Adaptación del bias sólo en reposo.
static constexpr float kBiasAlpha = 0.04f;

// Tras cualquier movimiento de AZ o ELEV, esperar este tiempo completo
// antes de volver a aprender el bias.
static constexpr unsigned long kBiasIdleDelayMs = 2000UL;


// Movimiento manual de AZ.
// Arranque claramente por encima del ruido residual; una vez detectado,
// se mantiene hasta llevar un tiempo realmente parado.
// El umbral de arranque debe quedar claramente por encima de la deriva que
// puede aparecer al cambiar la temperatura del JY901. Con 0,25 deg/s un
// salto de bias sostenido podia declararse giro manual; desde ese instante
// se suspendia el aprendizaje del bias y el falso giro quedaba enclavado.
// Se exige 1,00 deg/s mantenido y con sentido estable: así los cruces breves
// provocados por deriva no pueden enclavar una falsa rotación indefinida.
static constexpr float kManualAzStartDps = 1.00f;
static constexpr float kManualAzHoldDps  = 0.30f;
static constexpr unsigned long kManualAzStopMs = 800UL;
// Ignorar inercia y vibraciones inmediatamente después de usar un motor.
// Debe ser mayor que kBiasIdleDelayMs: al acabar una maniobra el bias puede
// haber cambiado ligeramente (temperatura, vibración o alimentación). Si la
// detección manual se habilita justo cuando empieza a reaprenderse el bias,
// ese residuo puede declararse movimiento manual y bloquear para siempre el
// propio aprendizaje, haciendo derivar angleZ con el rotor parado.
static constexpr unsigned long kManualAzMotorGraceMs =
    kBiasIdleDelayMs + 2000UL;
// Una muestra aislada no basta para declarar movimiento manual.
static constexpr unsigned long kManualAzConfirmMs = 1200UL;

// Aprendizaje del eje 3D real de azimut durante movimientos con motor.
// El vector canónico apunta en el sentido de AZ creciente.
static constexpr float kAzAxisLearnAlpha = 0.08f;
static constexpr float kAzAxisLearnMinDps = 0.40f;

static bool gImuConfigured = false;
static bool gImuAzMovingMode = false;

// +1 derecha, -1 izquierda, 0 parado.
static int8_t gAzMotionDirection = 0;

// Posición virtual mecánica.
static bool gRotorAzInitialized = false;
static bool gAzReferenceSet = false;
static float gRotorAzDeg = 0.0f;

// Persistencia unificada: posicion AZ + referencias fisicas + offset EL.
static constexpr uint32_t kStoreMagic = 0x52545232UL;
static constexpr uint8_t kStAzPos = 0x01;
static constexpr uint8_t kStAzLeft = 0x02;
static constexpr uint8_t kStElLow = 0x04;
static constexpr uint8_t kStElOff = 0x08;
static constexpr float kAzStoreStepDeg = 5.0f;

struct RotorStore {
  uint32_t magic;
  uint32_t seq;
  float az;
  float azLeft;
  float elLow;
  float elOff;
  uint8_t flags;
  uint8_t pad[3];
  uint32_t check;
};

static RotorStore gStore = {};
static float gAzTravelSinceStore = 0.0f;
static bool gWasAzMoving = false;

static bool gGyroBiasReady = false;
static float gBiasGx = 0.0f;
static float gBiasGy = 0.0f;
static float gBiasGz = 0.0f;

static unsigned long gLastIntegrationMs = 0;
static unsigned long gBiasIdleSinceMs = 0;

static float gRawElevationDeg = 0.0f;
static float gElevationOffsetDeg = 0.0f;
static float gCorrectedGyroRateDps = 0.0f;

// El JY901 está montado de forma que su roll disminuye cuando la antena
// sube. La elevación mecánica, en cambio, debe aumentar al subir.
static float correctedElevationDeg(float rawDeg) {
  return -rawDeg + gElevationOffsetDeg;
}


// Para el montaje actual, AZ creciente produce principalmente GZ negativo.
// Esta orientación sirve como arranque; después se refina automáticamente
// con los movimientos motorizados reales.
static float gAzAxisX = 0.0f;
static float gAzAxisY = 0.0f;
static float gAzAxisZ = -1.0f;

static bool gManualAzActive = false;
static unsigned long gManualAzLastMotionMs = 0;
static float gManualAzSignedRateDps = 0.0f;
static unsigned long gLastMotorMotionMs = 0;
static unsigned long gManualAzCandidateMs = 0;
static int8_t gManualAzCandidateDirection = 0;

static float normalize360(float v) {
  while (v < 0.0f) v += 360.0f;
  while (v >= 360.0f) v -= 360.0f;
  return v;
}

static uint32_t storeCheck(const RotorStore& r) {
  union U { float f; uint32_t u; } v;
  uint32_t c = r.magic ^ r.seq ^ uint32_t(r.flags) ^ 0x6A51C39DUL;
  v.f = r.az;     c ^= v.u;
  v.f = r.azLeft; c ^= v.u;
  v.f = r.elLow;  c ^= v.u;
  v.f = r.elOff;  c ^= v.u;
  return c;
}

static bool readStore(const char* path, RotorStore& r) {
  File f = FFat.open(path, "r");
  if (!f) return false;
  const size_t n = f.read(reinterpret_cast<uint8_t*>(&r), sizeof(r));
  f.close();
  return n == sizeof(r) &&
         r.magic == kStoreMagic &&
         r.check == storeCheck(r);
}

static bool saveStore() {
  if (!isStorageReady()) return false;

  gStore.magic = kStoreMagic;
  ++gStore.seq;
  if (gAzReferenceSet) {
    gStore.az = normalize360(gRotorAzDeg);
    gStore.flags |= kStAzPos;
  }
  gStore.check = storeCheck(gStore);

  const char* path = (gStore.seq & 1UL) ? "/az1.bin" : "/az0.bin";
  File f = FFat.open(path, "w");
  if (!f) return false;
  const size_t n = f.write(
      reinterpret_cast<const uint8_t*>(&gStore), sizeof(gStore));
  f.close();
  if (n == sizeof(gStore)) gAzTravelSinceStore = 0.0f;
  return n == sizeof(gStore);
}

bool restoreImuAzimuthReference() {
  if (simulationMode || !isStorageReady()) return false;

  RotorStore a, b;
  const bool va = readStore("/az0.bin", a);
  const bool vb = readStore("/az1.bin", b);

  if (!va && !vb) {
    gAzReferenceSet = false;
    Serial.println("AZ REF?");
    return false;
  }

  if (va && vb)
    gStore = ((int32_t)(a.seq - b.seq) > 0) ? a : b;
  else
    gStore = va ? a : b;

  gElevationOffsetDeg =
      (gStore.flags & kStElOff) ? gStore.elOff : 0.0f;

  if (!(gStore.flags & kStAzPos)) {
    gAzReferenceSet = false;
    Serial.println("AZ REF?");
    return false;
  }

  gRotorAzDeg = normalize360(gStore.az);
  angleZ = gRotorAzDeg;
  gRotorAzInitialized = true;
  gAzReferenceSet = true;
  gAzTravelSinceStore = 0.0f;
  gLastIntegrationMs = 0;
  Serial.print("AZ=");
  Serial.println(angleZ, 1);
  return true;
}

bool saveAzLeftLimitReference(float azLeft) {
  if (!isfinite(azLeft) || azLeft < 0.0f || azLeft >= 360.0f)
    return false;

  const float v = normalize360(azLeft);
  const bool changed =
      !(gStore.flags & kStAzLeft) ||
      fabsf(normalize360(gStore.azLeft) - v) > 0.01f;

  gStore.azLeft = v;
  gStore.flags |= kStAzLeft;

  if (changed) {
    gAzReferenceSet = false;
    gStore.flags &= ~kStAzPos;
  }
  return saveStore();
}

bool saveElLowerLimitReference(float elLow) {
  if (!isfinite(elLow) || elLow < -20.0f || elLow > 2.5f)
    return false;

  const bool changed =
      !(gStore.flags & kStElLow) ||
      fabsf(gStore.elLow - elLow) > 0.01f;

  gStore.elLow = elLow;
  gStore.flags |= kStElLow;

  if (changed) {
    gElevationOffsetDeg = 0.0f;
    gStore.elOff = 0.0f;
    gStore.flags &= ~kStElOff;
  }
  return saveStore();
}

bool saveRotorReferenceLimits(float azLeft, float elLow) {
  if (!isfinite(azLeft) || !isfinite(elLow) ||
      azLeft < 0.0f || azLeft >= 360.0f ||
      elLow < -20.0f || elLow > 2.5f) return false;

  const bool azChanged =
      !(gStore.flags & kStAzLeft) ||
      fabsf(normalize360(gStore.azLeft) - normalize360(azLeft)) > 0.01f;
  const bool elChanged =
      !(gStore.flags & kStElLow) ||
      fabsf(gStore.elLow - elLow) > 0.01f;

  gStore.azLeft = normalize360(azLeft);
  gStore.elLow = elLow;
  gStore.flags |= kStAzLeft | kStElLow;

  if (azChanged) {
    gAzReferenceSet = false;
    gStore.flags &= ~kStAzPos;
  }
  if (elChanged) {
    gElevationOffsetDeg = 0.0f;
    gStore.elOff = 0.0f;
    gStore.flags &= ~kStElOff;
  }
  return saveStore();
}

float getAzLeftLimitReference() {
  return (gStore.flags & kStAzLeft) ? gStore.azLeft : 0.0f;
}

float getElLowerLimitReference() {
  return (gStore.flags & kStElLow) ? gStore.elLow : 0.0f;
}

bool hasAzLeftLimitReference() {
  return gStore.flags & kStAzLeft;
}

bool hasElLowerLimitReference() {
  return gStore.flags & kStElLow;
}

void saveImuElevationOffset(float offsetDeg) {
  gElevationOffsetDeg = offsetDeg;
  gStore.elOff = offsetDeg;
  gStore.flags |= kStElOff;
  angleX = correctedElevationDeg(gRawElevationDeg);
  saveStore();
}

static void sendJY901Command(
    uint8_t reg,
    uint16_t value,
    uint16_t settleMs = 20) {

  const uint8_t cmd[5] = {
    0xFF,
    0xAA,
    reg,
    uint8_t(value & 0xFF),
    uint8_t((value >> 8) & 0xFF)
  };

  mySerial.write(cmd, sizeof(cmd));
  mySerial.flush();

  if (settleMs > 0) {
    delay(settleMs);
  }
}

static void unlockJY901() {
  const uint8_t unlockCmd[5] = {
    0xFF, 0xAA, 0x69, 0x88, 0xB5
  };

  mySerial.write(unlockCmd, sizeof(unlockCmd));
  mySerial.flush();
  delay(20);
}

static void configureJY901ForRotor() {
  unlockJY901();

  // Mantener 9 ejes para roll/pitch y diagnóstico.
  // YAWraw NO participa en el azimut del rotor.
  sendJY901Command(0x24, 0x0000, 40);

  // Umbral Z para la lógica interna del JY901.
  sendJY901Command(0x6F, kWzStaticIdle, 30);
  sendJY901Command(0x6E, kWzTimeIdleMs, 50);

  gImuConfigured = true;
  gImuAzMovingMode = false;

}

static void setJY901AzimuthMovingMode(bool moving) {
  if (!gImuConfigured) return;
  if (moving == gImuAzMovingMode) return;

  unlockJY901();

  const uint16_t threshold =
      moving ? kWzStaticMoving : kWzStaticIdle;

  sendJY901Command(0x6F, threshold, 20);

  gImuAzMovingMode = moving;

}

void setImuAzimuthMotionDirection(int8_t direction) {
  if (direction > 0) {
    gAzMotionDirection = 1;
  } else if (direction < 0) {
    gAzMotionDirection = -1;
  } else {
    gAzMotionDirection = 0;
  }

  // Nunca integrar el tiempo transcurrido antes del cambio de estado.
  gLastIntegrationMs = 0;
}

void setImuAzimuthReference(float azDeg) {
  gRotorAzDeg = normalize360(azDeg);
  angleZ = gRotorAzDeg;
  gRotorAzInitialized = true;
  gAzReferenceSet = true;
  gLastIntegrationMs = 0;
  gAzTravelSinceStore = 0.0f;
  gStore.flags |= kStAzPos;
  saveStore();
}

bool isImuAzimuthReferenceSet() {
  return gAzReferenceSet;
}

void clearImuAzimuthReference(bool eraseStored) {
  gAzReferenceSet = false;
  gLastIntegrationMs = 0;
  gWasAzMoving = false;
  if (eraseStored) {
    gStore.flags &= ~kStAzPos;
    saveStore();
  }
}

float getImuAzimuthRateDps() {
  return gCorrectedGyroRateDps;
}

float getImuRawElevationDeg() {
  return gRawElevationDeg;
}

void setImuElevationOffset(float offsetDeg) {
  gElevationOffsetDeg = offsetDeg;
  angleX = correctedElevationDeg(gRawElevationDeg);
}

static void normalizeAzAxis() {
  const float n =
      sqrtf(gAzAxisX * gAzAxisX +
            gAzAxisY * gAzAxisY +
            gAzAxisZ * gAzAxisZ);

  if (n < 0.20f) {
    gAzAxisX = 0.0f;
    gAzAxisY = 0.0f;
    gAzAxisZ = -1.0f;
    return;
  }

  gAzAxisX /= n;
  gAzAxisY /= n;
  gAzAxisZ /= n;
}

static void learnAzAxis(
    float dx,
    float dy,
    float dz,
    float rate) {

  if (gAzMotionDirection == 0 ||
      rate < kAzAxisLearnMinDps) {
    return;
  }

  const float inv = 1.0f / rate;

  // Convertir cualquier sentido motor al vector canónico AZ creciente.
  const float dir = float(gAzMotionDirection);
  const float ux = dx * inv * dir;
  const float uy = dy * inv * dir;
  const float uz = dz * inv * dir;

  gAzAxisX += kAzAxisLearnAlpha * (ux - gAzAxisX);
  gAzAxisY += kAzAxisLearnAlpha * (uy - gAzAxisY);
  gAzAxisZ += kAzAxisLearnAlpha * (uz - gAzAxisZ);

  normalizeAzAxis();
}

static void finishManualAzMotion(unsigned long nowMs) {
  if (!gManualAzActive) return;

  gManualAzActive = false;
  gManualAzSignedRateDps = 0.0f;
  gLastIntegrationMs = 0;
  gBiasIdleSinceMs = nowMs;

  antazimut = angleZ;

  if (gAzReferenceSet) {
    saveStore();
  }
}

bool isImuManualAzimuthMoving() {
  return gManualAzActive;
}

static void updateGyroBias(
    float gx,
    float gy,
    float gz) {

  if (!gGyroBiasReady) {
    gBiasGx = gx;
    gBiasGy = gy;
    gBiasGz = gz;
    gGyroBiasReady = true;
    return;
  }

  gBiasGx += kBiasAlpha * (gx - gBiasGx);
  gBiasGy += kBiasAlpha * (gy - gBiasGy);
  gBiasGz += kBiasAlpha * (gz - gBiasGz);
}

void initImu() {
  mySerial.setRxBufferSize(1024);

  mySerial.begin(
      9600,
      SERIAL_8N1,
      kJY901RxPin,
      kJY901TxPin
  );

  delay(100);

  JY901.attach(mySerial);

  configureJY901ForRotor();

  // MUY IMPORTANTE:
  // NO obtener la posición inicial del magnetómetro.
  // Tras cada reinicio la posición mostrada puede arrancar en 0°, pero
  // NO se considera una referencia mecánica válida hasta que el usuario
  // pulse "Fijar AZ=0" con el rotor colocado físicamente en origen.
  gRotorAzDeg = normalize360(angleZ);
  gRotorAzInitialized = true;
  gAzReferenceSet = false;

  // Empezar a contar reposo desde este momento. El bias no se aprenderá
  // hasta llevar 2 s completos con ambos motores parados.
  gBiasIdleSinceMs = millis();

}

void updateAntennaPosition() {
  if (simulationMode) {
    return;
  }

  // Si en la muestra anterior ya se había detectado movimiento manual,
  // mantener al JY901 en modo de umbral bajo.
  setJY901AzimuthMovingMode(
      azMoveActive || gManualAzActive
  );

  if (mySerial.available() <= 0) {
    return;
  }

  JY901.receiveSerialData();
  gLastImuSampleMs = millis();

  gRawElevationDeg = JY901.getRoll();
  angleX = correctedElevationDeg(gRawElevationDeg);
  angleY = JY901.getPitch();

  const float gx = JY901.getGyroX();
  const float gy = JY901.getGyroY();
  const float gz = JY901.getGyroZ();

  if (!gRotorAzInitialized) {
    gRotorAzDeg = normalize360(angleZ);
    gRotorAzInitialized = true;
  }

  const unsigned long nowMs = millis();

  // Guardar la posición exacta al finalizar/cancelar un giro motorizado AZ.
  if (gWasAzMoving && !azMoveActive && gAzReferenceSet) {
    saveStore();
  }
  gWasAzMoving = azMoveActive;

  // Vector del giróscopo corregido por bias.
  float dx = 0.0f;
  float dy = 0.0f;
  float dz = 0.0f;

  gCorrectedGyroRateDps = 0.0f;

  if (gGyroBiasReady) {
    dx = gx - gBiasGx;
    dy = gy - gBiasGy;
    dz = gz - gBiasGz;

    float r =
        sqrtf(dx * dx + dy * dy + dz * dz);

    if (r < kGyroNoiseFloorDps) r = 0.0f;
    if (r > kMaxRotorRateDps) r = kMaxRotorRateDps;

    gCorrectedGyroRateDps = r;
  }

  // Mientras el motor AZ gira conocemos el signo real de la maniobra.
  // Aprovechar esas muestras para aprender el vector 3D del eje mecánico.
  if (azMoveActive &&
      gAzMotionDirection != 0 &&
      gGyroBiasReady) {
    learnAzAxis(
        dx,
        dy,
        dz,
        gCorrectedGyroRateDps
    );
  }

  // Proyección firmada sobre el eje AZ aprendido.
  // Positivo = AZ creciente; negativo = AZ decreciente.
  float projectedRate =
      dx * gAzAxisX +
      dy * gAzAxisY +
      dz * gAzAxisZ;

  if (projectedRate > kMaxRotorRateDps) {
    projectedRate = kMaxRotorRateDps;
  }
  if (projectedRate < -kMaxRotorRateDps) {
    projectedRate = -kMaxRotorRateDps;
  }

  // --------------------------------------------------------------
  // DETECCIÓN DE MOVIMIENTO MANUAL
  //
  // Sólo se acepta cuando:
  // - no hay ningún motor moviéndose;
  // - no hay una orientación automática en curso;
  // - existe referencia absoluta AZ válida.
  //
  // Así, un movimiento de elevación no puede confundirse con AZ manual.
  // --------------------------------------------------------------
  const bool canBeManualAz =
      !azMoveActive &&
      !elevMoveActive &&
      !orientMoveActive &&
      (gLastMotorMotionMs == 0 ||
       (nowMs - gLastMotorMotionMs) >= kManualAzMotorGraceMs) &&
      gAzReferenceSet &&
      gGyroBiasReady;

  if (azMoveActive || elevMoveActive || orientMoveActive) {
    gLastMotorMotionMs = nowMs;
    gManualAzCandidateMs = 0;
    gManualAzCandidateDirection = 0;
  }

  const float manualAbsRate = fabsf(projectedRate);

  if (canBeManualAz) {
    const bool movingNow =
        manualAbsRate >=
        (gManualAzActive
             ? kManualAzHoldDps
             : kManualAzStartDps);

    if (movingNow) {
      if (!gManualAzActive) {
        const int8_t candidateDirection =
            projectedRate >= 0.0f ? 1 : -1;

        // Una deriva/ruido puede cruzar el umbral en muestras sueltas y
        // cambiar de signo. Un giro físico debe conservar el sentido durante
        // todo el tiempo de confirmación antes de permitir que cambie AZ.
        if (gManualAzCandidateMs == 0 ||
            candidateDirection != gManualAzCandidateDirection) {
          gManualAzCandidateMs = nowMs;
          gManualAzCandidateDirection = candidateDirection;
        }
        if ((nowMs - gManualAzCandidateMs) >= kManualAzConfirmMs) {
          gManualAzActive = true;
          gManualAzCandidateMs = 0;
          gManualAzCandidateDirection = 0;
          gLastIntegrationMs = nowMs;

          if (enableseguirsat) {
            enableseguirsat = false;
            addDiagnosticEvent("Seguimiento cancelado: giro manual AZ");
          }
        }
      }

      gManualAzSignedRateDps = projectedRate;
      gManualAzLastMotionMs = nowMs;
    } else {
      gManualAzCandidateMs = 0;
      gManualAzCandidateDirection = 0;
      if (gManualAzActive &&
          (nowMs - gManualAzLastMotionMs) >= kManualAzStopMs) {
        finishManualAzMotion(nowMs);
      }
    }
  } else {
    gManualAzCandidateMs = 0;
    gManualAzCandidateDirection = 0;
    finishManualAzMotion(nowMs);
  }

  // --------------------------------------------------------------
  // APRENDIZAJE DEL BIAS
  //
  // El movimiento manual cuenta también como movimiento físico.
  // De este modo jamás se "aprende" una rotación manual como nuevo bias.
  // --------------------------------------------------------------
  const bool anyPhysicalMovement =
      azMoveActive ||
      elevMoveActive ||
      gManualAzActive;

  if (anyPhysicalMovement) {
    gBiasIdleSinceMs = 0;
  } else if (gBiasIdleSinceMs == 0) {
    gBiasIdleSinceMs = nowMs;
  }

  const bool biasLearningAllowed =
      !anyPhysicalMovement &&
      gBiasIdleSinceMs != 0 &&
      (nowMs - gBiasIdleSinceMs) >=
          kBiasIdleDelayMs;

  // --------------------------------------------------------------
  // INTEGRACIÓN AZ
  // --------------------------------------------------------------
  if (azMoveActive &&
      gAzMotionDirection != 0 &&
      gGyroBiasReady &&
      gAzReferenceSet) {

    // Motor: mantener el sistema ya probado,
    // magnitud 3D + signo conocido de la orden.
    if (gLastIntegrationMs == 0) {
      gLastIntegrationMs = nowMs;
    } else {
      float dt =
          (nowMs - gLastIntegrationMs) / 1000.0f;

      gLastIntegrationMs = nowMs;

      if (dt > 0.50f) dt = 0.50f;

      const float stepDeg =
          gCorrectedGyroRateDps * dt;

      gRotorAzDeg = normalize360(
          gRotorAzDeg +
          float(gAzMotionDirection) * stepDeg
      );

      gAzTravelSinceStore += stepDeg;

      if (gAzTravelSinceStore >= kAzStoreStepDeg) {
        saveStore();
      }
    }

  } else if (gManualAzActive &&
             gAzReferenceSet) {

    // Manual: el signo sale de la proyección sobre el eje AZ aprendido.
    if (gLastIntegrationMs == 0) {
      gLastIntegrationMs = nowMs;
    } else {
      float dt =
          (nowMs - gLastIntegrationMs) / 1000.0f;

      gLastIntegrationMs = nowMs;

      if (dt > 0.50f) dt = 0.50f;

      float signedRate = gManualAzSignedRateDps;

      // Durante la pequeña cola de parada no integrar ruido.
      if (fabsf(signedRate) < kManualAzHoldDps) {
        signedRate = 0.0f;
      }

      const float stepDeg = signedRate * dt;

      gRotorAzDeg = normalize360(
          gRotorAzDeg + stepDeg
      );

      gAzTravelSinceStore += fabsf(stepDeg);

      // La posición "actual" del control también debe seguir el giro manual.
      antazimut = gRotorAzDeg;

      if (gAzTravelSinceStore >= kAzStoreStepDeg) {
        saveStore();
      }
    }

  } else {
    if (biasLearningAllowed) {
      updateGyroBias(gx, gy, gz);
    }

    gLastIntegrationMs = 0;
  }

  angleZ = normalize360(gRotorAzDeg);

  // Si la detección manual cambió en esta misma muestra,
  // actualizar inmediatamente el modo interno del JY901.
  setJY901AzimuthMovingMode(
      azMoveActive || gManualAzActive
  );
}

uint32_t getImuSampleAgeMs() {
  if (simulationMode) return 0;
  if (gLastImuSampleMs == 0) return UINT32_MAX;
  return millis() - gLastImuSampleMs;
}

bool isImuDataHealthy() {
  return simulationMode || getImuSampleAgeMs() <= 500UL;
}
