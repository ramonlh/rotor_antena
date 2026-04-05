# Cambio realizado: separación de config.h

Ahora `config.h` solo actúa como fichero paraguas y reagrupa:

- `config_core.h` → tipos y constantes generales
- `config_pins.h` → asignación de pines
- `config_network.h` → Wi‑Fi, IP fija y AP
- `config_services.h` → OTA y API N2YO
- `config_motion.h` → tiempos de movimiento

Ventaja:
- no has tenido que tocar todos los `#include "config.h"` del proyecto;
- pero ya puedes localizar cada parámetro en su sitio.

Siguiente paso recomendable:
- sacar credenciales y claves a `config_secrets.h` y no subirlo a GitHub.
