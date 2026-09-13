# ESP32 Wake HTTP API

This document is a portable integration contract for clients and AI coding agents that need to control an ESP32 Wake device.

## Connection model

In station mode, the device starts both servers:

- HTTP: configured port, default `2443`
- HTTPS: HTTP port plus one, default `2444`
- Exception: when the configured HTTP port is `65535`, HTTPS uses `65534`

Example direct base URLs:

```text
http://192.168.1.94:2443
https://192.168.1.94:2444
```

A reverse proxy may add a path prefix. Preserve the complete base URL when appending endpoints:

```text
Base URL: https://example-proxy.invalid/port/32443
Status:   https://example-proxy.invalid/port/32443/stt
```

Do not reduce a proxy URL to only its origin.

In access-point setup mode:

- SSID: `ESP32-Wake-Setup`
- Password: `12345678`
- HTTP setup page: `http://192.168.4.1/`
- HTTPS is disabled so captive-portal detection can work.

## Authentication contract

The same control password protects status and state-changing API endpoints.

| ESP32 password state | Required request | Result |
|---|---|---|
| No control password | `GET`, no password | Accepted |
| No control password | `POST` with any password | `403` |
| Control password configured | `POST` with correct password | Accepted |
| Control password configured | `GET` | `401` |
| Control password configured | `POST` with wrong password | `403` |

For authenticated requests, send the password as an URL-encoded form field:

```http
Content-Type: application/x-www-form-urlencoded;charset=UTF-8

password=your-password
```

Do not send the password in the URL or query string.

### Client decision rule

```javascript
function authOptions(password) {
  return password
    ? {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
        },
        body: new URLSearchParams({ password }),
      }
    : { method: "GET" };
}
```

The client-side password state must match the ESP32 password state. A client that stores a password while the ESP32 has no password will receive `403`.

## API summary

| Purpose | Canonical path | Aliases | Methods |
|---|---|---|---|
| Read computer state | `/status` | `/stt` | `GET`, `POST`, `OPTIONS` |
| Toggle/wake computer | `/wake` | `/pw` | `GET`, `POST`, `OPTIONS` |
| Force shutdown | `/shutdown` | `/sd`, `/fsd` | `GET`, `POST`, `OPTIONS` |
| Factory reset | `/reset` | `/api/reset`, `/rs` | `GET`, `POST`, `OPTIONS` |
| Configuration page | `/` | None | `GET` |
| Configuration login | `/login` | None | `POST` |
| Scan WiFi | `/api/scan` | None | `GET` |
| Save configuration | `/api/config` | None | `POST` |
| Reset from configuration page | `/api/config/reset` | None | `POST` |

Use the authentication contract above for status, wake, shutdown, and reset endpoints.

## Status

### `GET|POST /status`
### `GET|POST /stt`

Returns the computer state. `/stt` and `/status` currently return the same legacy JSON shape:

```json
{
  "pc0": {
    "GPIO": "5",
    "idx": "0",
    "stat": "connected"
  }
}
```

`Content-Type` is currently `text/plain`, although the body is valid JSON. Parse it with `response.json()`.

Possible `stat` values:

| Value | Meaning |
|---|---|
| `connected` | The target computer replied to ping. |
| `powered` | The configured Power LED input reports power, but ping failed. |
| `off` | Ping failed and the Power LED input does not report power. |

Important state limitation: when Power LED sensing is disabled, a failed ping is reported as `off`; the device cannot distinguish powered-but-unreachable from powered off.

The online check makes up to three ping attempts of approximately 250 ms each.

Example without a configured password:

```bash
curl http://192.168.1.94:2443/stt
```

Example with a configured password:

```bash
curl -k -X POST https://192.168.1.94:2444/stt \
  -H "Content-Type: application/x-www-form-urlencoded" \
  --data-urlencode "password=your-password"
```

JavaScript:

```javascript
const response = await fetch(`${baseUrl}/stt`, {
  cache: "no-store",
  ...authOptions(savedPassword),
});
if (!response.ok) throw new Error(`HTTP ${response.status}`);
const status = await response.json();
const computerState = status.pc0.stat;
```

## Wake or toggle power

### `GET|POST /wake`
### `GET|POST /pw`

Pulses the relay briefly. The physical result depends on the computer's current state and power-button configuration. In the dashboard this endpoint is used both to wake an off computer and request a normal shutdown when the computer is connected.

Successful response:

```text
HTTP 200
OK
```

Examples:

```bash
# ESP32 has no control password
curl http://192.168.1.94:2443/pw

# ESP32 has a control password
curl -k -X POST https://192.168.1.94:2444/pw \
  --data-urlencode "password=your-password"
```

## Force shutdown

### `GET|POST /shutdown`
### `GET|POST /sd`
### `GET|POST /fsd`

Holds or activates the relay using the firmware's force-shutdown behavior. Use this when the computer is powered but not responding to ping.

The relay remains active for approximately seven seconds. The HTTP response is sent only after that delay, so clients need a timeout longer than seven seconds.

Successful response:

```text
HTTP 200
OK
```

Example:

```bash
curl -k -X POST https://192.168.1.94:2444/sd \
  --data-urlencode "password=your-password"
```

## Factory reset

### `GET|POST /reset`
### `GET|POST /api/reset`
### `GET|POST /rs`

Destructive operation. It clears saved configuration, disconnects WiFi, stops the servers, and restarts the ESP32. The device then returns to setup/AP behavior.

Successful response before restart:

```text
HTTP 200
OK
```

Use the same GET/POST authentication rule. Always require explicit user confirmation before calling this endpoint. The server restarts shortly after writing the response, so some clients may observe a disconnect instead of receiving the complete `200 OK` response. Treat either outcome as indeterminate until the device reappears in setup mode.

## Configuration UI and session login

### `GET /`

Returns the HTML configuration page.

- With no control password, the configuration page is returned directly.
- With a control password, the login page is returned until the browser has a valid `esp32auth` session cookie.
- The session token is stored in RAM and is invalidated by reboot or password removal.

### `POST /login`

Form fields:

| Field | Required | Description |
|---|---|---|
| `password` | Yes when protected | Plain control password, URL encoded. |

On success:

```text
HTTP 302
Location: ./
Set-Cookie: esp32auth=<session-token>; Path=/
```

`Location: ./` intentionally preserves reverse-proxy path prefixes.

When the ESP32 has no control password, `POST /login` also succeeds. This supports a dashboard that still has an old locally saved password during the transition to password-free operation.

Session limitations:

- The ESP32 stores only one session token in RAM. A later successful login replaces the prior token.
- The cookie uses `Path=/`. Multiple ESP32 devices exposed below different prefixes on the same proxy origin can overwrite each other's `esp32auth` cookie.
- A reboot or removal of the control password invalidates the in-memory token.

### `POST /api/config/reset`

Requires a valid configuration-page session cookie when a control password exists. This endpoint is used by the **Reset all configuration** button because the page does not retain the plaintext control password.

It erases all saved settings and restarts the ESP32 in setup/AP mode. The page asks for explicit confirmation before sending the request. A disconnect immediately after the request can be expected while the device restarts.

Browser auto-login pattern:

```javascript
const target = `esp-config-${profileId}`;
const popup = window.open("about:blank", target);
if (!popup) throw new Error("Pop-up blocked");

const form = document.createElement("form");
form.method = "POST";
form.action = `${baseUrl}/login`;
form.target = target;
form.hidden = true;

const field = document.createElement("input");
field.type = "hidden";
field.name = "password";
field.value = savedPassword;
form.append(field);
document.body.append(form);
form.submit();
form.remove();
```

Open the popup synchronously inside a user click handler or browsers may block it.

## Scan WiFi networks

### `GET /api/scan`

Returns nearby networks:

```json
{
  "networks": [
    { "ssid": "Example WiFi", "rssi": -51 }
  ]
}
```

If the ESP32 WiFi scan driver fails:

```text
HTTP 503
```

```json
{
  "error": "scan_failed",
  "networks": []
}
```

An empty successful scan returns HTTP 200 with an empty `networks` array.

This endpoint is not protected by the control-password API check.

## Save configuration

### `POST /api/config`

Requires an authorized configuration-page session when a control password exists. Authenticate through `/login` first and retain the `esp32auth` cookie.

Form fields:

| Field | Type | Behavior |
|---|---|---|
| `ssid` | string | SSID selected from scan results. |
| `ssid_custom` | string | Manual SSID; takes precedence over `ssid` when non-empty. |
| `password` | string | New WiFi password. Blank normally retains the existing password. During a required reconnect, firmware tries the saved password and then an empty password; if the open-network attempt succeeds, the empty password becomes active. |
| `port` | integer | HTTP server port. The UI constrains it to `1..65535`, but server-side parsing currently narrows directly to 16 bits; integrations must validate this range before sending. |
| `target_ip` | string | Intended target computer IPv4 address. Blank retains the current value; server-side IPv4 validation is not performed during save. |
| `use_power_led_pin` | checkbox (`1`) | Enables physical power-state sensing. Absence means disabled. |
| `power_led_pin` | integer | GPIO used for Power LED sensing. The server rejects GPIO 5 but does not enforce the UI's `0..21` range. Missing values default to GPIO 2. |
| `admin_password` | string | New control password. Blank retains the existing password. |
| `remove_admin_password` | checkbox (`1`) | Clears the control password and invalidates the current session. Takes precedence over `admin_password`. |
| `auto_reconnect` | checkbox (`1`) | Enables periodic saved-WiFi reconnect. Absence means disabled. |

Success responses:

```json
{
  "status": "saved_and_connected",
  "ssid": "Example WiFi",
  "target_ip": "192.168.1.135",
  "port": 2443
}
```

or:

```json
{
  "status": "saved_but_wifi_not_connected",
  "ssid": "Example WiFi",
  "target_ip": "192.168.1.135",
  "port": 2443
}
```

Relevant errors:

| Status | Cause |
|---|---|
| `400` | Missing SSID, or Power LED GPIO conflicts with relay GPIO 5. |
| `401` | Protected configuration endpoint called without a valid session cookie. |

The JSON responses are assembled manually by the current firmware. SSID and target strings are HTML-escaped rather than fully JSON-escaped, so unusual values containing backslashes or control characters may produce invalid JSON. Keep these configuration values to normal printable network-name/IP characters.

Removing the ESP32 password is intentionally separate from submitting an empty password:

```text
admin_password=
remove_admin_password=1
```

After removal, status/control clients must switch from authenticated POST to unauthenticated GET.

## CORS and browser behavior

Status and control endpoints return:

```http
Access-Control-Allow-Origin: *
Access-Control-Allow-Private-Network: true
```

Their `OPTIONS` preflight response is `HTTP 200 OK` with body `OK` and includes:

```http
Access-Control-Allow-Methods: GET, POST, OPTIONS
Access-Control-Allow-Headers: Content-Type
Access-Control-Allow-Private-Network: true
Access-Control-Max-Age: 600
```

This permits dashboards deployed on different domains. Authentication is still enforced by the endpoint method/password contract.

Browser constraints still apply:

1. An HTTPS dashboard cannot fetch an HTTP ESP32 because browsers block mixed content. Use the ESP32 HTTPS port or an HTTPS reverse proxy/tunnel.
2. Direct ESP32 HTTPS uses an embedded self-signed certificate. The user may need to open the ESP32 HTTPS URL manually and approve the browser warning first.
3. JavaScript cannot automatically bypass a TLS certificate warning.
4. Some mobile browsers may refuse self-signed or hostname-mismatched certificates entirely. A trusted HTTPS reverse proxy is the reliable solution.
5. Private Network Access policies may vary by browser even though the firmware responds to PNA preflight.

For command-line testing of the current self-signed certificate, `curl -k` disables certificate verification. Do not copy that behavior into production browser security logic.

## Error handling guidance

| HTTP/result | Client interpretation |
|---|---|
| `200` | Request accepted. Parse status JSON or text response as documented. |
| `302` | Successful configuration login; follow redirect and retain cookie. |
| `400` | Invalid configuration input. |
| `401` | ESP32 has a password but the request used GET, a config session is missing, or `/login` received a missing/incorrect password. |
| `403` | Wrong password, or client sent password POST while ESP32 has no password. |
| `404` | Unknown route while not in AP captive-portal mode. |
| `503` | WiFi scan driver failure. |
| Network/TLS error | Device unreachable, mixed-content block, unapproved certificate, proxy failure, or local-network browser policy. |

Do not classify `401` or `403` as device offline. Report an authentication mismatch and let the user update or clear the locally saved password.

## Recommended reusable client

```javascript
function joinEndpoint(baseUrl, path) {
  return `${baseUrl.replace(/\/+$/, "")}/${path.replace(/^\/+/, "")}`;
}

function authOptions(password) {
  if (!password) return { method: "GET" };
  return {
    method: "POST",
    headers: {
      "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
    },
    body: new URLSearchParams({ password }),
  };
}

async function callEsp32(baseUrl, path, password, timeoutMs = 12000) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const response = await fetch(joinEndpoint(baseUrl, path), {
      cache: "no-store",
      ...authOptions(password),
      signal: controller.signal,
    });

    if (response.status === 401 || response.status === 403) {
      throw new Error(`ESP32 authentication mismatch (HTTP ${response.status})`);
    }
    if (!response.ok) {
      throw new Error(`ESP32 request failed (HTTP ${response.status})`);
    }
    return response;
  } finally {
    clearTimeout(timeout);
  }
}

async function readComputerState(baseUrl, password) {
  const response = await callEsp32(baseUrl, "stt", password);
  const data = await response.json();
  return data.pc0.stat;
}

async function togglePower(baseUrl, password) {
  await callEsp32(baseUrl, "pw", password);
}

async function forceShutdown(baseUrl, password) {
  await callEsp32(baseUrl, "sd", password);
}
```

## Captive-portal routes

These GET routes are intended for operating-system captive-portal detection, not application integrations:

```text
/generate_204
/gen_204
/hotspot-detect.html
/library/test/success.html
/ncsi.txt
/connecttest.txt
/canonical.html
/success.txt
```

Unknown GET paths are redirected to setup while the ESP32 is in AP/AP+STA mode. Outside that mode, unknown paths return `404`.

In AP/AP+STA mode, each listed captive-probe route returns HTTP 200 with an HTML meta-refresh to `/`. Outside AP/AP+STA mode, it returns HTTP 404.
