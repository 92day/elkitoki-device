# Elkitoki Device Protocol

This document fixes the first shared contract between:

- Arduino firmware in `elkitoki-device`
- the laptop serial bridge
- `elkitoki-server`
- `elkitoki-web`

All serial messages are newline-delimited UTF-8 JSON.

## Outbound messages from Arduino

### 1. Periodic status

Sent every 1 second.

```json
{
  "kind": "status",
  "device": "uno-main",
  "heartRaw": 612,
  "fingerDetected": true,
  "redLed": false,
  "greenLed": true,
  "manualPressed": false,
  "soundA": 0,
  "soundB": 0,
  "soundC": 0,
  "tiltAlert": false,
  "motorActive": false
}
```

Notes:

- `heartRaw` is the direct analog value from the pulse sensor.
- `soundA/B/C` should be `0` until the sound sensors are enabled in firmware.
- `tiltAlert` is `false` until the tilt sensor is enabled in firmware.

### 2. Event message

Sent when a meaningful state change happens.

```json
{
  "kind": "event",
  "eventType": "worker_call_button",
  "source": "manual_button",
  "active": true
}
```

Expected `eventType` values:

- `worker_call_button`
- `heart_abnormal`
- `noise_abnormal`
- `fall_detected`
- `command_applied`

Examples:

```json
{
  "kind": "event",
  "eventType": "heart_abnormal",
  "worker": "A",
  "heartRaw": 745,
  "threshold": 700,
  "active": true
}
```

```json
{
  "kind": "event",
  "eventType": "noise_abnormal",
  "zone": "B",
  "value": 812,
  "threshold": 700,
  "active": true
}
```

```json
{
  "kind": "event",
  "eventType": "fall_detected",
  "worker": "A",
  "active": true
}
```

### 3. Error message

```json
{
  "kind": "error",
  "message": "unknown command"
}
```

## Inbound commands to Arduino

Commands are also newline-delimited JSON.

### Worker call

```json
{
  "cmd": "call_worker",
  "worker": "A"
}
```

Expected device behavior:

- turn on red LED
- activate vibration motor if enabled
- show `CALL A` on OLED

### Clear outputs

```json
{
  "cmd": "clear_outputs"
}
```

Expected device behavior:

- turn off red LED
- turn on green LED
- stop vibration motor

### Set indicator

```json
{
  "cmd": "set_indicator",
  "color": "red",
  "state": "on"
}
```

Supported colors:

- `red`
- `green`

Supported states:

- `on`
- `off`

## Bridge responsibilities

The laptop bridge should:

- open the Arduino serial port
- read each JSON line
- forward status and events to `elkitoki-server`
- receive worker-call actions from server-side APIs or sockets
- write JSON commands back to Arduino

## Server responsibilities

The backend should:

- store raw sensor logs in MongoDB
- create alert records for abnormal heart/noise/fall/manual-call events
- provide latest sensor state to the web dashboard
- expose a command channel for worker call actions
