require('dotenv').config();

const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');

const SERIAL_PORT = process.env.SERIAL_PORT || 'COM3';
const SERIAL_BAUDRATE = Number(process.env.SERIAL_BAUDRATE || 115200);
const SERVER_BASE = (process.env.SERVER_BASE || 'http://127.0.0.1:8000').replace(/\/$/, '');
const DEVICE_NAME = process.env.DEVICE_NAME || 'uno-main';
const BRIDGE_ID = process.env.BRIDGE_ID || 'laptop-bridge-1';
const COMMAND_POLL_MS = Number(process.env.COMMAND_POLL_MS || 1500);

if (typeof fetch !== 'function') {
  console.error('[Bridge] Node 18+ is required because global fetch is not available.');
  process.exit(1);
}

const port = new SerialPort({
  path: SERIAL_PORT,
  baudRate: SERIAL_BAUDRATE,
});

const parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));
let commandPollTimer = null;

async function postJson(path, payload) {
  const response = await fetch(`${SERVER_BASE}${path}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  });

  if (!response.ok) {
    const message = await response.text();
    throw new Error(message || `${response.status}`);
  }

  return response.status === 204 ? null : response.json();
}

async function getJson(path) {
  const response = await fetch(`${SERVER_BASE}${path}`);
  if (!response.ok) {
    const message = await response.text();
    throw new Error(message || `${response.status}`);
  }
  return response.json();
}

async function handleIncomingLine(rawLine) {
  const line = rawLine.trim();
  if (!line) return;

  let payload;
  try {
    payload = JSON.parse(line);
  } catch (error) {
    console.warn('[Bridge] Invalid JSON from Arduino:', line);
    return;
  }

  try {
    await postJson('/api/sensors/events', payload);
    console.log('[Bridge] forwarded sensor payload:', payload.kind || 'unknown');
  } catch (error) {
    console.error('[Bridge] failed to forward sensor payload:', error.message);
  }
}

async function pollCommands() {
  try {
    const commands = await getJson(`/api/device/commands/pending?device=${encodeURIComponent(DEVICE_NAME)}`);
    if (!Array.isArray(commands) || commands.length === 0) return;

    for (const command of commands) {
      const serialPayload = {
        cmd: command.cmd,
        worker: command.worker || undefined,
        color: command.color || undefined,
        state: command.state || undefined,
        ...command.payload,
      };

      await new Promise((resolve, reject) => {
        port.write(`${JSON.stringify(serialPayload)}\n`, (error) => {
          if (error) {
            reject(error);
            return;
          }
          port.drain((drainError) => (drainError ? reject(drainError) : resolve()));
        });
      });

      await postJson(`/api/device/commands/${command.id}/ack`, { bridge_id: BRIDGE_ID });
      console.log('[Bridge] command sent to Arduino:', command.cmd, command.worker || '');
    }
  } catch (error) {
    console.error('[Bridge] failed to poll/send commands:', error.message);
  }
}

port.on('open', () => {
  console.log(`[Bridge] serial port opened: ${SERIAL_PORT} @ ${SERIAL_BAUDRATE}`);
  commandPollTimer = setInterval(pollCommands, COMMAND_POLL_MS);
});

port.on('error', (error) => {
  console.error('[Bridge] serial port error:', error.message);
});

parser.on('data', (line) => {
  void handleIncomingLine(line);
});

process.on('SIGINT', () => {
  if (commandPollTimer) clearInterval(commandPollTimer);
  console.log('\n[Bridge] shutting down...');
  port.close(() => process.exit(0));
});
