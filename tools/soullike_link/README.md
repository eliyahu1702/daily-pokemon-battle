# Soullike Link

Lets two copies of Pokémon Soullike, each running in its own mGBA, play together over the internet.

```
[Player 1: mGBA + ROM + bridge.lua] <--TCP--> [relay.py] <--TCP--> [Player 2: mGBA + ROM + bridge.lua]
```

- **The ROM** (`src/soullike_link.c`) reads and writes a small mailbox in memory. It never touches the network.
- **bridge.lua** runs inside mGBA and copies messages between that mailbox and the relay.
- **relay.py** pairs the two players in a room, hands out the Daily Run seed, and forwards messages.

## Playing

You need mGBA 0.10 or newer on both computers (scripting was added in 0.10).

1. **One person runs the relay** somewhere both of you can reach. That can be one player's computer (forward the port, or use a VPN like Tailscale) or any small server:

   ```sh
   python3 tools/soullike_link/relay.py --port 7777
   ```

   It only needs Python 3, no extra packages.

2. **Both players set up the bridge.** Open `tools/soullike_link/bridge.lua` and set the three values at the top:

   ```lua
   RELAY_HOST = "203.0.113.5"  -- address of the relay
   RELAY_PORT = 7777
   ROOM = "ash-and-misty"      -- any name, the same for both of you
   ```

3. **Both players load the game and the script.** Open the ROM in mGBA, then go to **Tools > Scripting**, and in that window **File > Load script** and pick `bridge.lua`. The scripting window shows when you've joined the room and when your partner arrives.

To check the link in-game (debug builds): hold **R** and press **Start** in the overworld, then pick **Soullike Link**. It shows the connection, the ping to your partner and today's Daily Run date.

## Testing

```sh
# Relay unit tests
python3 -m unittest tools/soullike_link/test_relay.py

# Two headless emulators running the real ROM, linked through the relay
tools/soullike_link/e2e/build_mgba.sh /tmp/mgba        # once: libmgba with Lua scripting
make -C tools/soullike_link/e2e MGBA_DIR=/tmp/mgba
python3 tools/soullike_link/e2e/run_e2e.py tools/soullike_link/e2e/harness pokeemerald.gba
```

CI runs both on every pull request.

## Protocol

Relay frames are a big-endian `u16` length followed by that many bytes; the first byte is the frame kind.

| Kind | Direction | Body |
|---|---|---|
| 1 HELLO | bridge → relay | `u8` link version, then the room name |
| 2 WELCOME | relay → bridge | `u8` player (0 or 1), `u32` Daily Run seed, `u32` date as YYYYMMDD |
| 3 DATA | both | `u8` message type, then up to 60 bytes of payload |
| 4 PEER | relay → bridge | `u8` 1 when the partner is in the room, 0 when they left |
| 5 ERROR | relay → bridge | reason text; the relay closes the connection |

The mailbox layout is documented in `include/soullike_link.h`. Game message types are `enum SoullikeLinkMsgType`; the link itself only uses PING and PONG so far.
