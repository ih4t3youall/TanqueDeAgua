# Servidor — Medidor de tanque de agua

Aplicación web para monitorear y controlar el tanque. El nodo tanque
(ESP32) se conecta acá.

## Qué hace

- **Login** con usuario y contraseña para entrar a la web (sesión por cookie).
- **Token de dispositivo**: el ESP32 debe mandar el header `X-API-Token`
  correcto en cada reporte; sin él, el servidor lo rechaza (401).
- El nodo tanque **reporta cada 10 segundos** el nivel de agua.
- **Modo automático**: la bomba arranca cuando el nivel baja del **30 %**
  y corta cuando llega al **80 %**. Los umbrales se pueden **cambiar desde
  la web** (tarjeta "Umbrales del modo automático"); el nodo los recibe en
  su próximo reporte y se persisten en `config.json`.
- **Modo manual**: la bomba se enciende/apaga desde la web (deja de ser
  automático hasta que vuelvas a AUTO).

## Cómo correrlo

```bash
cd servidor
python3 -m venv venv            # solo la primera vez
./venv/bin/pip install -r requirements.txt
./venv/bin/python app.py        # escucha en http://0.0.0.0:5000
```

Si el puerto 5000 está ocupado (en macOS lo usa AirPlay):

```bash
PORT=8090 ./venv/bin/python app.py
```

Después entrá desde el navegador a `http://IP-DEL-SERVIDOR:5000`.

## Credenciales

Se guardan en `config.json` (se crea solo la primera vez):

| Qué | Dónde cambiarlo |
|---|---|
| Usuario web (`admin`) | `config.json` → `web_user` |
| Contraseña web | `./venv/bin/python app.py --set-password` (se guarda hasheada, nunca en texto plano) |
| Token del ESP32 (`kame-tank-7f3a9c2e51d84b06`) | `config.json` → `device_token` y `API_TOKEN` en `nodo_tanque.ino` (deben coincidir) |

Si el `config.json` no tiene contraseña (primer arranque), el servidor genera
una al azar y la muestra **una sola vez** por consola.

Reiniciá el servidor después de editar `config.json`.

## Seguridad (para exponerlo a internet)

- La contraseña web se guarda **hasheada** (scrypt); `config.json` queda con
  permisos `600`.
- **Anti fuerza bruta**: 5 intentos fallidos de login (o de token del ESP32)
  por IP → esa IP queda bloqueada 10 minutos.
- Headers de seguridad (CSP, `X-Frame-Options`, `nosniff`) en todas las
  respuestas, y cookie de sesión `HttpOnly` + `SameSite`.
- El body de los requests se limita a 16 KB y los datos que reporta el nodo
  se validan y acotan a rangos razonables.
- Sirve con **waitress** (servidor de producción) si está instalado.
- Si adelante hay un reverse proxy con HTTPS (recomendado), corré con
  `TRUST_PROXY=1` para que el bloqueo por IP use la IP real del cliente y la
  cookie salga con el flag `secure`.
- Pendiente conocido: el ESP32 habla HTTP plano (el token viaja sin cifrar).
  Si el hosting da HTTPS, se puede pasar el nodo a `WiFiClientSecure`.

## API

| Endpoint | Quién lo usa | Autenticación |
|---|---|---|
| `POST /api/status` | ESP32: reporta estado, recibe config | header `X-API-Token` |
| `GET /api/state` | Web: refresca el dashboard | cookie de sesión |
| `POST /api/control` | Web: cambia modo / bomba manual | cookie de sesión |
| `GET/POST /login`, `GET /logout` | Web | — |

## Recordatorio del lado del ESP32

En `nodo_tanque/nodo_tanque.ino`:
- `SERVER_URL` → IP y puerto donde corre este servidor.
- `API_TOKEN` → igual a `device_token` de `config.json`.
- WiFi: red `KameHouse` (hardcodeada a propósito).
- Reporta cada 10 s; umbrales por defecto 30 % / 80 %.
