#!/usr/bin/env python3
"""Pokémon Soullike relay server.

Both players' mGBA bridge scripts connect here and join the same room.
The relay pairs them up, hands out the Daily Run seed, and forwards
messages between the two games. It never looks inside game messages.

Run it with:  python3 tools/soullike_link/relay.py --port 7777
Only the Python standard library is needed.
"""

import argparse
import asyncio
import datetime
import hashlib
import logging
import struct

LINK_VERSION = 1

KIND_HELLO = 1
KIND_WELCOME = 2
KIND_DATA = 3
KIND_PEER = 4
KIND_ERROR = 5

MAX_FRAME = 1024
PLAYERS_PER_ROOM = 2

log = logging.getLogger("soullike-relay")


def daily_seed(date):
    """The Daily Run seed is derived from the UTC date, so every room gets the same run that day."""
    digest = hashlib.sha256(f"soullike-daily-{date.isoformat()}".encode()).digest()
    return int.from_bytes(digest[:4], "big")


def encode_frame(kind, body=b""):
    payload = bytes([kind]) + body
    return struct.pack(">H", len(payload)) + payload


async def read_frame(reader):
    header = await reader.readexactly(2)
    (length,) = struct.unpack(">H", header)
    if length == 0 or length > MAX_FRAME:
        raise ValueError(f"bad frame length {length}")
    payload = await reader.readexactly(length)
    return payload[0], payload[1:]


class Relay:
    def __init__(self, today=None):
        self.rooms = {}
        self._today = today or (lambda: datetime.datetime.now(datetime.timezone.utc).date())

    async def handle(self, reader, writer):
        peer = writer.get_extra_info("peername")
        room = slot = None
        try:
            kind, body = await asyncio.wait_for(read_frame(reader), timeout=10)
            if kind != KIND_HELLO or len(body) < 1:
                await self._refuse(writer, "expected HELLO")
                return
            if body[0] != LINK_VERSION:
                await self._refuse(writer, f"version {body[0]} not supported, relay is version {LINK_VERSION}")
                return

            room = body[1:].decode("utf-8", "replace") or "soullike"
            players = self.rooms.setdefault(room, [None] * PLAYERS_PER_ROOM)
            if None not in players:
                await self._refuse(writer, f"room '{room}' is full")
                room = None
                return
            slot = players.index(None)
            players[slot] = writer

            today = self._today()
            date_number = today.year * 10000 + today.month * 100 + today.day
            writer.write(encode_frame(KIND_WELCOME, struct.pack(">BII", slot, daily_seed(today), date_number)))
            log.info("%s joined room '%s' as player %d", peer, room, slot + 1)

            partner = players[1 - slot]
            if partner is not None:
                writer.write(encode_frame(KIND_PEER, b"\x01"))
                partner.write(encode_frame(KIND_PEER, b"\x01"))
                await partner.drain()
            await writer.drain()

            while True:
                kind, body = await read_frame(reader)
                if kind != KIND_DATA:
                    continue
                partner = players[1 - slot]
                if partner is not None:
                    partner.write(encode_frame(KIND_DATA, body))
                    await partner.drain()
        except (asyncio.IncompleteReadError, ConnectionError, asyncio.TimeoutError, ValueError) as err:
            log.debug("%s closed: %r", peer, err)
        finally:
            if room is not None and slot is not None:
                players = self.rooms[room]
                players[slot] = None
                partner = players[1 - slot]
                if partner is not None:
                    try:
                        partner.write(encode_frame(KIND_PEER, b"\x00"))
                        await partner.drain()
                    except ConnectionError:
                        pass
                else:
                    del self.rooms[room]
                log.info("player %d left room '%s'", slot + 1, room)
            writer.close()

    async def _refuse(self, writer, reason):
        log.info("refused %s: %s", writer.get_extra_info("peername"), reason)
        writer.write(encode_frame(KIND_ERROR, reason.encode()))
        await writer.drain()


async def serve(host, port):
    relay = Relay()
    server = await asyncio.start_server(relay.handle, host, port)
    log.info("Soullike relay listening on %s:%d", host, port)
    async with server:
        await server.serve_forever()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="0.0.0.0", help="address to listen on (default: all)")
    parser.add_argument("--port", type=int, default=7777)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO, format="%(asctime)s %(message)s")
    try:
        asyncio.run(serve(args.host, args.port))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
