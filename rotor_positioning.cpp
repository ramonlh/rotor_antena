

#include <Arduino.h>
#include <math.h>
#include "rotor_positioning.h"
#include "rotor_axes.h"
#include "rotor_io.h"
#include "imu_sensor.h"
#include "app_state.h"
#include "config.h"
#include "diagnostic_log.h"

enum RefState : uint8_t {
  REF_IDLE,
  REF_AZ_LEFT,
  REF_AZ_RELEASE,
  REF_EL_LOW
};
static RefState gRefState = REF_IDLE;
static unsigned long gRefT0 = 0;
static unsigned long gRefT1 = 0;
static float gRefLast = 0.0f;
static bool gRefSaw = false;
static bool gRefSearchOnly = false;
static bool gAzCalFound = false;
static bool gElCalFound = false;
static char gLastSafetyStop[64] = "Ninguna";
static float gRefTravelDeg = 0.0f;
static unsigned long gRefSampleMs = 0;
static bool gAzReleaseSucceeded = false;

static constexpr unsigned long kRefNoStartMs = 6000UL;
static constexpr unsigned long kRefStillMs = 1800UL;
static constexpr float kRefAzMovingRate = 0.25f;
static constexpr float kRefAzStoppedRate = 0.12f;
static constexpr float kRefElProgressDeg = 0.12f;
static constexpr float kAzReleaseDeg = 2.0f;
// La liberacion de emergencia no necesita una posicion absoluta. Se mide
// exclusivamente la distancia recorrida con la velocidad del giroscopio.
static constexpr float kAzManualReleaseDeg = 3.0f;
static constexpr unsigned long kAzManualReleaseMaxMs = 12000UL;

// No iniciar un movimiento AZ si ya estamos practicamente en destino.
static constexpr float kAzStartToleranceDeg = 0.20f;

static float normalizeAzimuth(float azim) {
  while (azim < 0.0f) azim += 360.0f;
  while (azim >= 360.0f) azim -= 360.0f;
  return azim;
}

static float currentCableCenterAzimuth() {
  return hasAzLeftLimitReference()
      ? normalizeAzimuth(getAzLeftLimitReference() + 180.0f)
      : kAzCableCenterDeg;
}

// Convierte el azimut 0..360 en una referencia inicial
// comprendida entre -180 y +180.
static float azimuthToSignedCableReference(float azimuth) {
  float relative =
      normalizeAzimuth(azimuth - currentCableCenterAzimuth());

  if (relative > 180.0f) {
    relative -= 360.0f;
  }

  return relative;
}

// Convierte una posición física de cable a azimut 0..360.
static float cableAngleToAzimuth(float cableAngle) {
  return normalizeAzimuth(
      cableAngle + currentCableCenterAzimuth()
  );
}

static float clampElevation(float el) {
  if (el < kMinElevationDeg) return kMinElevationDeg;
  if (el > 90.0f) return 90.0f;
  return el;
}

static float angularDistanceDeg(float a, float b) {
  a = normalizeAzimuth(a);
  b = normalizeAzimuth(b);

  float distance = fabsf(a - b);

  if (distance > 180.0f) {
    distance = 360.0f - distance;
  }

  return distance;
}

static float signedAngularDeltaDeg(
    float fromAngle,
    float toAngle) {

  float delta =
      normalizeAzimuth(toAngle) -
      normalizeAzimuth(fromAngle);

  if (delta > 180.0f) {
    delta -= 360.0f;
  }

  if (delta < -180.0f) {
    delta += 360.0f;
  }

  return delta;
}

static float azLastObservedAngleDeg = 0.0f;
static unsigned long azLastProgressMs = 0;
static unsigned long azMaxMoveMs = 0;

// Seguridad de azimut:
// - sólo el avance en el SENTIDO ORDENADO cuenta como progreso;
// - si la lectura retrocede de forma sostenida, parar enseguida;
// - si no hay avance real durante 6 s, parar.
static constexpr float kAzProgressStepDeg = 0.05f;
static constexpr float kAzWrongDirectionStepDeg = 0.05f;
static constexpr float kAzWrongDirectionLimitDeg = 1.50f;
static constexpr unsigned long kAzWrongDirectionTimeoutMs = 2500UL;
static constexpr unsigned long kAzNoProgressTimeoutMs = 6000UL;

static float azWrongDirectionAccumDeg = 0.0f;
static unsigned long azWrongDirectionStartMs = 0;

// Velocidad mínima conservadora para el timeout total.
// El rotor real medido está alrededor de 1 grado/segundo.
static constexpr float kAzSafetyMinSpeedDegPerSec = 0.50f;

// Posición física absoluta del cable.
// Con los límites actuales -180..+180 sólo existe una vuelta útil,
// por lo que no hace falta acumular vueltas entre lecturas.
static float azCablePositionDeg = 0.0f;
static float azCableTargetDeg = 0.0f;

static void updateAzCableTracker() {
  float relative =
      normalizeAzimuth(angleZ - currentCableCenterAzimuth());

  if (relative > 180.0f) {
    relative -= 360.0f;
  }

  // A 180 grados existen dos extremos equivalentes. Conservar
  // el lado desde el que llegamos evita saltar de -180 a +180.
  if (fabsf(relative - 180.0f) < 0.5f &&
      azCablePositionDeg < 0.0f) {
    relative = -180.0f;
  }

  azCablePositionDeg = relative;
}

static bool selectCableTargetForAzimuth(
    float requestedAzimuth,
    float& selectedTargetDeg) {

  float target =
      azimuthToSignedCableReference(requestedAzimuth);

  // Igual que para la posición: 180 puede ser +180 o -180. En un recorrido
  // asimétrico cuyo máximo es menor que +180, sólo -180 representa un punto
  // físico válido (el final izquierdo).
  if (fabsf(target - 180.0f) < 0.01f &&
      (kAzCableMaxDeg < 180.0f || azCablePositionDeg < 0.0f)) {
    target = -180.0f;
  }

  if (target < kAzCableMinDeg ||
      target > kAzCableMaxDeg) {
    return false;
  }

  selectedTargetDeg = target;
  return true;
}

bool isRotorReferenceBusy() {
  return gRefState != REF_IDLE;
}

static void abortReference() {
  disableElevation();
  disableRotation();
  setImuAzimuthMotionDirection(0);
  elevMoveActive = false;
  azMoveActive = false;
  gRefState = REF_IDLE;
  Serial.println("REF FAIL");
}

static bool startAzLeftSeek(bool applyStoredReference) {
  if (simulationMode ||
      isRotorReferenceBusy() ||
      isImuManualAzimuthMoving()) return false;
  if (applyStoredReference && !hasAzLeftLimitReference())
    return false;

  // Una orden explicita de calibracion sustituye al seguimiento o a un
  // desplazamiento automatico anterior. Parar antes de invertir relés.
  const bool wasMoving = isAnyMotionBusy();
  enableseguirsat = false;
  stopAllMotion();
  if (wasMoving) delay(100);
  motionStoppedByUser = false;

  // Al buscar el final izquierdo se pierde necesariamente la posición AZ
  // anterior, tanto en un referenciado como en una calibración inicial.
  // Sin invalidarla, el IMU continúa integrando sobre una referencia que ya
  // no es válida y, una vez parado en el tope, cualquier residuo del gyro
  // puede hacer variar continuamente el AZ mostrado en calibracion.html.
  // La nueva referencia se establecerá al aplicar el valor medido (calibrar)
  // o al recuperar el valor del final guardado (referenciar).
  clearImuAzimuthReference(true);

  gRefSearchOnly = !applyStoredReference;
  gAzCalFound = false;
  gRefState = REF_AZ_LEFT;
  gRefT0 = gRefT1 = millis();
  gRefSaw = false;

  disableRotation();
  digitalWrite(kGiro1pin, ON);
  digitalWrite(kGiro2pin, ON);
  setImuAzimuthMotionDirection(-1);
  digitalWrite(kGiropin, ON);
  azMoveActive = true;
  return true;
}

bool startAzLeftReference() {
  return startAzLeftSeek(true);
}

bool startAzLeftLimitRelease() {
  if (simulationMode ||
      isRotorReferenceBusy() ||
      isImuManualAzimuthMoving()) return false;

  // Esta es la unica maniobra AZ permitida sin referencia: siempre se aleja
  // del final izquierdo, recorre como maximo 3 grados medidos por el IMU y
  // dispone ademas de watchdog de arranque y tiempo total.
  const bool wasMoving = isAnyMotionBusy();
  enableseguirsat = false;
  stopAllMotion();
  if (wasMoving) delay(100);
  motionStoppedByUser = false;

  gRefState = REF_AZ_RELEASE;
  gRefT0 = gRefT1 = millis();
  gRefSampleMs = gRefT0;
  gRefTravelDeg = 0.0f;
  gRefSaw = false;
  gAzReleaseSucceeded = false;

  disableRotation();
  // OFF/OFF aumenta AZ: se separa del final izquierdo.
  digitalWrite(kGiro1pin, OFF);
  digitalWrite(kGiro2pin, OFF);
  setImuAzimuthMotionDirection(+1);
  digitalWrite(kGiropin, ON);
  azMoveActive = true;
  return true;
}

bool startAzLeftCalibrationSearch() {
  return startAzLeftSeek(false);
}

static bool startElLowerSeek(bool applyStoredReference) {
  if (simulationMode ||
      isRotorReferenceBusy()) return false;
  if (applyStoredReference && !hasElLowerLimitReference())
    return false;

  const bool wasMoving = isAnyMotionBusy();
  enableseguirsat = false;
  stopAllMotion();
  if (wasMoving) delay(100);
  motionStoppedByUser = false;

  gRefSearchOnly = !applyStoredReference;
  gElCalFound = false;
  gRefState = REF_EL_LOW;
  gRefT0 = gRefT1 = millis();
  gRefLast = getImuRawElevationDeg();
  gRefSaw = false;

  disableElevation();
  setElevationDirectionDown();
  enableElevation();
  elevMoveActive = true;
  return true;
}

bool startElLowerReference() {
  return startElLowerSeek(true);
}

bool startElLowerCalibrationSearch() {
  return startElLowerSeek(false);
}

void updateRotorReference() {
  if (gRefState == REF_IDLE) return;
  const unsigned long now = millis();

  if (gRefState == REF_AZ_LEFT) {
    const float rate = getImuAzimuthRateDps();

    if (rate >= kRefAzMovingRate) {
      gRefSaw = true;
      gRefT1 = now;
    } else if (gRefSaw && rate <= kRefAzStoppedRate &&
               now - gRefT1 >= kRefStillMs) {
      disableRotation();
      azMoveActive = false;
      setImuAzimuthMotionDirection(0);
      azCablePositionDeg = kAzCableMinDeg;
      azCableTargetDeg = kAzCableMinDeg;
      gRefState = REF_IDLE;

      if (gRefSearchOnly) {
        gAzCalFound = true;
        gRefSearchOnly = false;
        return;
      }

      const float ref = getAzLeftLimitReference();
      setImuAzimuthReference(ref);
      startAzimuthMove(normalizeAzimuth(ref + kAzReleaseDeg));
      return;
    }

    if ((!gRefSaw && now - gRefT0 > kRefNoStartMs) ||
        now - gRefT0 > 330000UL) {
      abortReference();
    }
    return;
  }

  if (gRefState == REF_AZ_RELEASE) {
    const float rate = getImuAzimuthRateDps();
    float dt = (now - gRefSampleMs) / 1000.0f;
    gRefSampleMs = now;
    if (dt > 0.25f) dt = 0.25f;

    if (rate >= kRefAzMovingRate) {
      gRefSaw = true;
      gRefT1 = now;
      gRefTravelDeg += rate * dt;
    }

    if (gRefTravelDeg >= kAzManualReleaseDeg) {
      disableRotation();
      setImuAzimuthMotionDirection(0);
      azMoveActive = false;
      gRefState = REF_IDLE;
      gAzReleaseSucceeded = true;
      addDiagnosticEvent("AZ liberado; falta referenciar");
      return;
    }

    if ((!gRefSaw && now - gRefT0 > kRefNoStartMs) ||
        now - gRefT0 > kAzManualReleaseMaxMs) {
      stopMotionBySafety("AZ liberar sin avance");
    }
    return;
  }

  const float raw = getImuRawElevationDeg();
  if (fabsf(raw - gRefLast) >= kRefElProgressDeg) {
    gRefSaw = true;
    gRefLast = raw;
    gRefT1 = now;
  }

  if (gRefSaw && now - gRefT1 >= kRefStillMs) {
    disableElevation();
    elevMoveActive = false;
    gRefState = REF_IDLE;

    if (gRefSearchOnly) {
      gElCalFound = true;
      gRefSearchOnly = false;
      return;
    }

    saveImuElevationOffset(
        getElLowerLimitReference() + getImuRawElevationDeg());
    startElevationMove(kMinElevationDeg);
    return;
  }

  if ((!gRefSaw && now - gRefT0 > kRefNoStartMs) ||
      now - gRefT0 > 120000UL) {
    abortReference();
  }
}

bool wasAzLeftLimitReleaseSuccessful() {
  return gAzReleaseSucceeded;
}


bool applyAzLeftCalibration(float azDeg) {
  if (!gAzCalFound || isAnyMotionBusy())
    return false;
  if (!saveAzLeftLimitReference(azDeg))
    return false;

  azCablePositionDeg = kAzCableMinDeg;
  azCableTargetDeg = kAzCableMinDeg;
  setImuAzimuthReference(azDeg);
  gAzCalFound = false;
  startAzimuthMove(normalizeAzimuth(azDeg + kAzReleaseDeg));
  return true;
}

bool applyElLowerCalibration(float elDeg) {
  if (!gElCalFound || isAnyMotionBusy())
    return false;
  if (!saveElLowerLimitReference(elDeg))
    return false;

  saveImuElevationOffset(elDeg + getImuRawElevationDeg());
  gElCalFound = false;
  startElevationMove(kMinElevationDeg);
  return true;
}

bool isAzLeftCalibrationFound() {
  return gAzCalFound;
}

bool isElLowerCalibrationFound() {
  return gElCalFound;
}

bool isOrientationBusy() {
  return orientMoveActive;
}

void stopMotionBySafety(const char* reason) {
  strlcpy(gLastSafetyStop, reason, sizeof(gLastSafetyStop));
  addDiagnosticEvent(reason);
  Serial.println(reason);

  stopAllMotion();

  // Evitar que el seguimiento vuelva a arrancar
  // automáticamente después del fallo.
  enableseguirsat = false;
}

const char* getLastSafetyStopReason() {
  return gLastSafetyStop;
}

void updateSimulationMotion() {
  if (!simulationMode) return;

  unsigned long now = millis();
  if (simLastUpdateMs == 0) {
    simLastUpdateMs = now;
    return;
  }

  float dt = (now - simLastUpdateMs) / 1000.0f;
  simLastUpdateMs = now;

  float azStep = simAzSpeedDegPerSec * dt;
  float elStep = simElSpeedDegPerSec * dt;

  // azimut
  float azDiff = simTargetAz - angleZ;
  if (azDiff > 180.0f) azDiff -= 360.0f;
  if (azDiff < -180.0f) azDiff += 360.0f;

  if (fabsf(azDiff) <= azStep) {
    angleZ = simTargetAz;
    azMoveActive = false;
  } else {
    angleZ += (azDiff > 0 ? azStep : -azStep);
  }

  while (angleZ < 0.0f) angleZ += 360.0f;
  while (angleZ >= 360.0f) angleZ -= 360.0f;

  // elevación
  float elDiff = simTargetEl - angleX;
  if (fabsf(elDiff) <= elStep) {
    angleX = simTargetEl;
    elevMoveActive = false;
  } else {
    angleX += (elDiff > 0 ? elStep : -elStep);
  }

  if (angleX < kMinElevationDeg) angleX = kMinElevationDeg;
  if (angleX > 90.0f) angleX = 90.0f;

  antazimut = angleZ;
  antelevat = angleX;
}

void startOrientationMove(float azim, float elev) {
  motionStoppedByUser = false;

  elev = clampElevation(elev);
  azim = normalizeAzimuth(azim);

  // cancelar orientación previa
  orientMoveActive = false;
  orientPhase = ORIENT_IDLE;

  orientTargetAz = azim;
  orientTargetEl = elev;

  // Validar AZ antes de mover elevacion. De lo contrario, una orden situada
  // en el sector fisicamente inaccesible moveria primero EL y sólo se
  // rechazaria varios segundos despues al intentar arrancar AZ.
  if (!simulationMode) {
    if (!isImuAzimuthReferenceSet()) {
      stopMotionBySafety("AZ sin ref");
      return;
    }

    float checkedTargetDeg = 0.0f;
    if (!selectCableTargetForAzimuth(azim, checkedTargetDeg)) {
      stopMotionBySafety("AZ sector prohibido");
      return;
    }
  }

  // arranca siempre por elevación, también en simulación
  startElevationMove(orientTargetEl);
  orientMoveActive = true;
  orientPhase = ORIENT_WAIT_ELEV;
}

void updateOrientationMove() {
  if (!orientMoveActive) {
    return;
  }

  switch (orientPhase) {
    case ORIENT_WAIT_ELEV:
      if (!isElevationBusy()) {
        startAzimuthMove(orientTargetAz);
        orientPhase = ORIENT_WAIT_AZ;
      }
      break;

    case ORIENT_WAIT_AZ:
      if (!isAzimuthBusy()) {
        if (!simulationMode) {
          updateAntennaPosition();
        }
        antazimut = angleZ;
        antelevat = angleX;
        orientMoveActive = false;
        orientPhase = ORIENT_IDLE;
      }
      break;

    default:
      orientMoveActive = false;
      orientPhase = ORIENT_IDLE;
      break;
  }
}

bool isAzimuthBusy() {
  return azMoveActive;
}

void startAzimuthMove(float azim) {
  motionStoppedByUser = false;
  azim = normalizeAzimuth(azim);

  if (simulationMode) {

    azTarget = azim;
    simTargetAz = azim;

    if (simLastUpdateMs == 0) {
      simLastUpdateMs = millis();
    }

    float azDiff = simTargetAz - angleZ;

    if (azDiff > 180.0f) azDiff -= 360.0f;
    if (azDiff < -180.0f) azDiff += 360.0f;

    if (fabsf(azDiff) <= 0.05f) {
      angleZ = simTargetAz;
      antazimut = angleZ;
      azMoveActive = false;
    } else {
      azMoveActive = true;
    }

    return;
  }

  // En modo real nunca mover AZ si no se ha establecido manualmente
  // la referencia mecánica después del último reinicio.
  if (!isImuAzimuthReferenceSet()) {
    disableRotation();
    setImuAzimuthMotionDirection(0);
    azMoveActive = false;

    stopMotionBySafety(
        "AZ sin ref"
    );
    return;
  }

  updateAntennaPosition();
  updateAzCableTracker();

  azTarget = azim;

  if (!selectCableTargetForAzimuth(
          azTarget,
          azCableTargetDeg)) {

    disableRotation();
    setImuAzimuthMotionDirection(0);
    azMoveActive = false;

    stopMotionBySafety(
        "AZ sector prohibido"
    );

    return;
  }

  const float distanceDeg =
      fabsf(azCableTargetDeg - azCablePositionDeg);

  if (distanceDeg <= kAzStartToleranceDeg) {
    disableRotation();
    setImuAzimuthMotionDirection(0);
    antazimut = angleZ;
    azMoveActive = false;
    return;
  }

  const unsigned long now = millis();

  azMoveStartMs = now;
  azLastProgressMs = now;
  azLastObservedAngleDeg = angleZ;
  azWrongDirectionAccumDeg = 0.0f;
  azWrongDirectionStartMs = 0;

  // Tiempo máximo absoluto de esta maniobra, calculado con una
  // velocidad mínima deliberadamente inferior a la real.
  azMaxMoveMs = calculateMoveTimeoutMs(
      distanceDeg,
      kAzSafetyMinSpeedDegPerSec
  );

  azMovePhase2 = false;

  azMoveDirectionRight =
      azCableTargetDeg > azCablePositionDeg;

  // El signo del giro se conoce por la orden del motor.
  // El IMU integrará únicamente la magnitud del gyro corregido.
  setImuAzimuthMotionDirection(
      azMoveDirectionRight ? +1 : -1
  );

  disableRotation();

  // En este rotor los niveles eléctricos de dirección están
  // invertidos respecto al sentido lógico del azimut:
  // OFF/OFF aumenta el ángulo (derecha) y ON/ON lo disminuye (izquierda).
  if (azMoveDirectionRight) {
    digitalWrite(kGiro1pin, OFF);
    digitalWrite(kGiro2pin, OFF);
  } else {
    digitalWrite(kGiro1pin, ON);
    digitalWrite(kGiro2pin, ON);
  }

  digitalWrite(kGiropin, ON);
  azMoveActive = true;
}

void updateAzimuthMove() {
  if (!azMoveActive) {
    return;
  }

  if (simulationMode) {
    return;
  }

  // angleZ se actualiza periodicamente desde loop().
  updateAzCableTracker();

  // Comprobar primero si hemos alcanzado el objetivo.
  // Es importante hacerlo antes de la protección dura porque
  // 0/360 grados puede corresponder exactamente a uno de los
  // extremos físicos válidos del cable.
  bool targetReached;

  if (azMoveDirectionRight) {
    // No anticipar la parada: esperar a alcanzar o superar el objetivo.
    targetReached =
        azCablePositionDeg >= azCableTargetDeg;
  } else {
    targetReached =
        azCablePositionDeg <= azCableTargetDeg;
  }

  if (targetReached) {
    disableRotation();
    setImuAzimuthMotionDirection(0);

    antazimut = angleZ;
    azMoveActive = false;
    azMovePhase2 = false;
    azWrongDirectionAccumDeg = 0.0f;
    azWrongDirectionStartMs = 0;

    return;
  }

  // Protección dura. Sólo se dispara si todavía NO hemos
  // alcanzado el destino y seguimos intentando salir de los
  // límites físicos permitidos.
  if (azMoveDirectionRight &&
      azCablePositionDeg >= kAzCableMaxDeg) {

    stopMotionBySafety(
        "AZ lim der"
    );

    return;
  }

  if (!azMoveDirectionRight &&
      azCablePositionDeg <= kAzCableMinDeg) {

    stopMotionBySafety(
        "AZ lim izq"
    );

    return;
  }

  const unsigned long now = millis();

  // ----------------------------------------------------------------
  // Watchdog DIRECCIONAL.
  //
  // Antes se aceptaba cualquier cambio de angleZ como progreso. Eso
  // permitía que una lectura errónea que retrocedía mantuviese el motor
  // encendido. Ahora sólo el movimiento en el sentido ordenado renueva
  // el watchdog.
  // ----------------------------------------------------------------
  const float signedStepDeg =
      signedAngularDeltaDeg(
          azLastObservedAngleDeg,
          angleZ
      );

  if (fabsf(signedStepDeg) >= kAzProgressStepDeg) {
    azLastObservedAngleDeg = angleZ;

    const bool stepInCorrectDirection =
        azMoveDirectionRight
            ? (signedStepDeg > 0.0f)
            : (signedStepDeg < 0.0f);

    if (stepInCorrectDirection) {
      azLastProgressMs = now;

      // El avance correcto cancela la sospecha anterior de sentido
      // incorrecto: toleramos pequeñas oscilaciones o ruido.
      azWrongDirectionAccumDeg = 0.0f;
      azWrongDirectionStartMs = 0;
    } else if (fabsf(signedStepDeg) >=
               kAzWrongDirectionStepDeg) {

      azWrongDirectionAccumDeg +=
          fabsf(signedStepDeg);

      if (azWrongDirectionStartMs == 0) {
        azWrongDirectionStartMs = now;
      }

      if (azWrongDirectionAccumDeg >=
              kAzWrongDirectionLimitDeg ||
          (now - azWrongDirectionStartMs) >=
              kAzWrongDirectionTimeoutMs) {

        stopMotionBySafety(
            "AZ sentido"
        );
        return;
      }
    }
  }

  // Si el sensor se congela o no existe avance en el sentido correcto
  // durante 6 s, no seguir alimentando el motor.
  if ((now - azLastProgressMs) >
      kAzNoProgressTimeoutMs) {

    stopMotionBySafety(
        "AZ sin progreso"
    );
    return;
  }

  // Segunda red de seguridad: ninguna maniobra puede durar más
  // de lo razonable para la distancia solicitada.
  if ((now - azMoveStartMs) > azMaxMoveMs) {
    stopMotionBySafety(
        "AZ timeout"
    );
    return;
  }

}

void setAzimuth(float azim) {
  startAzimuthMove(azim);

  while (isAzimuthBusy()) {
    updateAzimuthMove();
    delay(1);
  }
}

void orientAntenna(float azim, float elev) {
  startOrientationMove(azim, elev);
}

void jogAzimuth(float delta) {
  enableseguirsat = false;

  if (azMoveActive || orientMoveActive) {
    return;
  }

  motionStoppedByUser = false;

  updateAntennaPosition();
  updateAzCableTracker();

  float requestedCableDeg =
      azCablePositionDeg + delta;

  // Si el salto solicitado rebasa el recorrido físico, no descartar la
  // orden: avanzar hasta el extremo correspondiente. startAzimuthMove()
  // conserva los watchdogs y detiene el relé al alcanzar ese límite.
  if (requestedCableDeg > kAzCableMaxDeg)
    requestedCableDeg = kAzCableMaxDeg;
  if (requestedCableDeg < kAzCableMinDeg)
    requestedCableDeg = kAzCableMinDeg;

  const float newAz =
      cableAngleToAzimuth(requestedCableDeg);

  disableElevation();
  elevMoveActive = false;

  orientMoveActive = false;
  orientPhase = ORIENT_IDLE;

  orientTargetAz = newAz;
  orientTargetEl = angleX;

  startAzimuthMove(newAz);
}

void jogElevation(float delta) {
  enableseguirsat = false;

  // Un ajuste manual tiene prioridad sobre una orientación o un ajuste
  // anterior. Antes se desactivaba el seguimiento, pero se descartaba esta
  // orden silenciosamente si la elevación seguía ocupada; por eso el botón
  // Abajo parecía no funcionar durante (o justo después de) una maniobra.
  if (elevMoveActive || azMoveActive || orientMoveActive) {
    disableElevation();
    disableRotation();
    elevMoveActive = false;
    azMoveActive = false;
    orientMoveActive = false;
    orientPhase = ORIENT_IDLE;
    delay(100);  // Dar tiempo a los relés antes de invertir el sentido.
  }

  motionStoppedByUser = false;

  updateAntennaPosition();

  float startEl = angleX;
  float newEl = clampElevation(startEl + delta);

  // Un límite no debe invertir una orden manual. Por ejemplo, si el IMU
  // informa -3 grados y se pulsa Abajo, el clamp a +3 quedaría por encima
  // de la posición actual y startElevationMove() terminaría subiendo.
  const bool wouldReverseRequestedDirection =
      (delta < 0.0f && newEl >= startEl) ||
      (delta > 0.0f && newEl <= startEl);
  if (wouldReverseRequestedDirection) {
    disableElevation();
    elevMoveActive = false;
    antelevat = angleX;
    return;
  }

  // Arriba/abajo solo mueven elevación.
  disableRotation();
  azMoveActive = false;

  orientMoveActive = false;
  orientPhase = ORIENT_IDLE;

  orientTargetAz = angleZ;
  orientTargetEl = newEl;

  startElevationMove(newEl);
}

bool isAnyMotionBusy() {
  return isElevationBusy() ||
         isAzimuthBusy() ||
         isOrientationBusy() ||
         isRotorReferenceBusy() ||
         isImuManualAzimuthMoving();
}

void stopAllMotion() {
  disableElevation();
  disableRotation();
  setImuAzimuthMotionDirection(0);
  motionStoppedByUser = true;
  elevMoveActive = false;
  azMoveActive = false;
  orientMoveActive = false;

  orientPhase = ORIENT_IDLE;
  if (gRefState != REF_IDLE) {
    gAzCalFound = false;
    gElCalFound = false;
  }
  gRefState = REF_IDLE;
  gRefSearchOnly = false;

  // opcional pero útil: fijar la referencia actual como posición “alcanzada”
  antazimut = angleZ;
  antelevat = angleX;
}

const char* getMotionStatusText() {
  if (isImuManualAzimuthMoving()) return "MANUAL_AZ";
  if (gRefState == REF_AZ_LEFT) return "REF_AZ";
  if (gRefState == REF_AZ_RELEASE) return "LIBERAR_AZ";
  if (gRefState == REF_EL_LOW)  return "REF_EL";

  if (orientMoveActive) {
    if (orientPhase == ORIENT_WAIT_ELEV) return "ORIENT_ELEV";
    if (orientPhase == ORIENT_WAIT_AZ)   return "ORIENT_AZ";
    return "ORIENT";
  }

  if (isElevationBusy()) return "MOV_ELEV";
  if (isAzimuthBusy())   return "MOV_AZ";

  return "IDLE";
}
