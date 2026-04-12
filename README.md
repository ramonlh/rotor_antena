# rotor_antena

![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-blue.svg)
![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-00979D.svg)
![UI: Web](https://img.shields.io/badge/UI-Web%20Interface-2ea44f.svg)
![Status: In development](https://img.shields.io/badge/Status-In%20development-orange.svg)

Control de rotor de antena con dos motores DC, interfaz web integrada y funciones de seguimiento de satélites.

---

## Visión general

`rotor_antena` es un proyecto para controlar un rotor de antena en **azimut** y **elevación** desde un ESP32, con una interfaz web local y funciones orientadas a radioafición y seguimiento de satélites.

El firmware combina:

- control manual por pasos y por posición absoluta
- visualización web del estado del rotor
- seguimiento de satélites
- consulta de satélites visibles
- modo simulación para pruebas
- OTA, HTTP y FTP
- gestión de favoritos
- configuración modular por archivos

---

## Características principales

### Control del rotor

- Control de **dos ejes**: azimut y elevación
- Movimiento manual incremental
- Movimiento a posición absoluta
- Retorno a origen
- Estado de movimiento y orientación
- Modo simulación para validar la interfaz sin mover hardware

### Interfaz web

- Página principal accesible desde navegador
- Indicadores gráficos de azimut y elevación
- Visualización de posición actual y objetivo
- Selección de satélite desde lista
- Control manual desde la propia web
- Página de configuración y mantenimiento

### Funciones de satélites

- Catálogo base de satélites
- Favoritos almacenados
- Búsqueda por nombre
- Satélites visibles ahora
- Predicción/seguimiento
- Información de radio asociada al satélite

### Servicios auxiliares

- Servidor web HTTP
- OTA
- FTP
- Almacenamiento local
- Configuración separada por módulos

---

## Estructura del repositorio

```text
rotor_antena/
├── rotor_antena.ino
├── LICENSE
├── README.md
├── README_config_split.txt
│
├── app_state.*
├── state_imu.*
├── state_satellite.*
├── state_services.*
├── state_tracking.*
│
├── imu_sensor.*
├── rotor_io.*
├── rotor_axes.*
├── rotor_positioning.*
│
├── network_services.*
├── storage_services.*
│
├── web_handlers.*
├── web_pages.*
├── web_routes.*
├── web_ui.*
│
├── sat_catalog.*
├── sat_fetch.*
├── sat_favorites.*
├── sat_radio_info.*
├── sat_search.*
├── sat_tracking.*
├── sat_visibility.*
├── sat_visible_now.*
├── satellite_tracker.*
│
├── config.h
├── config_core.h
├── config_motion.h
├── config_network.h
├── config_pins.h
├── config_services.h
└── config_secrets_template.h
```

---

## Arquitectura del proyecto

### 1. Capa de hardware

- IMU / sensor de orientación
- motores de azimut y elevación
- entradas/salidas de control
- finales de carrera o referencias, si existen en el montaje

### 2. Capa de control

- cálculo de objetivos de azimut/elevación
- movimientos manuales
- secuencias de orientación
- simulación

### 3. Capa de servicios

- WiFi
- HTTP
- OTA
- FTP
- almacenamiento

### 4. Capa de aplicación

- seguimiento de satélites
- favoritos
- visibilidad
- información de radio
- interfaz web

---

## Requisitos

### Hardware

Como base, el proyecto está pensado para un entorno tipo:

- **ESP32**
- dos motores DC para rotor
- electrónica de potencia / driver adecuada
- sensor IMU
- conectividad WiFi
- estructura mecánica de rotor de azimut y elevación

### Software

- Arduino IDE o entorno compatible con ESP32
- Core ESP32 adecuado para tu placa
- Librerías necesarias según el proyecto, por ejemplo:
  - `ArduinoOTA`
  - `WebServer`
  - `HTTPClient`
  - `Wire`
  - `Arduino_JSON`
  - librería del sensor IMU utilizado

> Conviene revisar exactamente las librerías que tu entorno necesite según la versión del core ESP32 y el sensor instalado.

---

## Configuración

La configuración está separada en varios ficheros para que el proyecto sea más limpio y fácil de mantener:

- `config_core.h`
- `config_pins.h`
- `config_network.h`
- `config_services.h`
- `config_motion.h`
- `config.h`

Además, el repositorio incluye una plantilla:

- `config_secrets_template.h`

### Recomendación de uso

1. Copia `config_secrets_template.h`
2. Renómbralo a `config_secrets.h`
3. Rellena SSID, contraseña, claves o datos privados
4. Añade `config_secrets.h` a `.gitignore`

---

## Puesta en marcha rápida

### 1. Clonar el repositorio

```bash
git clone https://github.com/ramonlh/rotor_antena.git
```

### 2. Abrir el proyecto

Abre `rotor_antena.ino` en Arduino IDE.

### 3. Configurar

Revisa especialmente:

- pines de motores
- parámetros de red
- parámetros de movimiento
- IMU
- servicios OTA/FTP
- secretos y credenciales

### 4. Seleccionar placa y puerto

Elige tu placa ESP32 y el puerto serie correcto.

### 5. Compilar y cargar

Compila y sube el firmware al dispositivo.

### 6. Acceder a la interfaz web

Una vez conectado a la red del equipo, abre en el navegador la IP que muestre el ESP32 al arrancar.

---

## Flujo general de uso

1. Arranca el sistema
2. Comprueba conexión WiFi
3. Accede a la interfaz web
4. Verifica azimut/elevación actuales
5. Selecciona un satélite o usa control manual
6. Ordena un movimiento absoluto o incremental
7. Supervisa estado y posición objetivo

---

## Funciones disponibles en la web

### Página principal

- Estado general
- Conectividad
- Modo real / simulación
- Indicadores gráficos de rotor
- Selección de satélite
- Seguimiento
- Movimiento manual
- Posicionamiento absoluto

### Configuración

- Restaurar catálogo
- Recargar favoritos
- Buscar satélites visibles
- Añadir favoritos
- Buscar satélites por nombre

---

## Capturas de pantalla

Puedes añadir aquí más adelante una o varias imágenes de la interfaz web:

```md
![Pantalla principal](docs/images/main_page.png)
![Pantalla de configuración](docs/images/config_page.png)
```

---

## Estado actual del repositorio

Actualmente el repositorio público incluye el sketch principal, los módulos del rotor, web, red, IMU, almacenamiento, satélites y varios archivos de configuración, y su `README.md` publicado todavía es muy mínimo. citeturn832070view0

---

## Mejoras futuras sugeridas

- esquema eléctrico completo
- lista de materiales
- guía de calibración
- explicación del sistema mecánico del rotor
- capturas reales de la interfaz
- documentación del sensor IMU
- explicación de límites y referencias de azimut/elevación
- documentación de instalación paso a paso
- vídeo o GIF de funcionamiento

---

## Solución de problemas

### La web se recarga demasiado

Revisar:

- refrescos automáticos de la página
- JavaScript de actualización parcial
- qué bloques HTML se están refrescando

### El rotor no alcanza la posición esperada

Revisar:

- calibración del sensor
- lógica de jog manual
- límites de elevación
- normalización de azimut
- tiempos de movimiento y frenado

### No aparecen satélites o predicciones

Revisar:

- conexión WiFi
- servicios HTTP
- disponibilidad de la fuente de datos
- configuración de red

---

## Licencia

Este proyecto se distribuye bajo licencia **GPL-3.0**. El repositorio público también marca esa misma licencia. citeturn832070view0

---

## Autor

**Ramón Lorenzo**

---

## Nota final

Este README está pensado como una base más completa para GitHub. Se puede ampliar fácilmente con una sección específica de hardware real, materiales, cableado y calibración cuando quieras dejar el repositorio más cerrado y documentado.
