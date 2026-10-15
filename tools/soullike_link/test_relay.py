"""Tests for the Soullike relay. Run with: python3 -m unittest tools/soullike_link/test_relay.py"""

import asyncio
import datetime
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import relay  # noqa: E402

TODAY = datetime.date(2026, 10, 15)


class Client:
    def __init__(self, reader, writer):
        self.reader = reader
        self.writer = writer

    @classmethod
    async def join(cls, port, room=b"test", version=relay.LINK_VERSION):
        reader, writer = await asyncio.open_connection("127.0.0.1", port)
        writer.write(relay.encode_frame(relay.KIND_HELLO, bytes([version]) + room))
        await writer.drain()
        return cls(reader, writer)

    async def frame(self):
        return await asyncio.wait_for(relay.read_frame(self.reader), timeout=2)

    async def send(self, body):
        self.writer.write(relay.encode_frame(relay.KIND_DATA, body))
        await self.writer.drain()

    async def close(self):
        self.writer.close()
        await self.writer.wait_closed()


class RelayTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.relay = relay.Relay(today=lambda: TODAY)
        self.server = await asyncio.start_server(self.relay.handle, "127.0.0.1", 0)
        self.port = self.server.sockets[0].getsockname()[1]

    async def asyncTearDown(self):
        self.server.close()
        await self.server.wait_closed()

    async def test_two_players_pair_up_and_get_the_daily_seed(self):
        a = await Client.join(self.port)
        kind, body = await a.frame()
        self.assertEqual(kind, relay.KIND_WELCOME)
        player, seed, date = struct.unpack(">BII", body)
        self.assertEqual(player, 0)
        self.assertEqual(seed, relay.daily_seed(TODAY))
        self.assertEqual(date, 20261015)

        b = await Client.join(self.port)
        kind, body = await b.frame()
        self.assertEqual((kind, body[0]), (relay.KIND_WELCOME, 1))
        self.assertEqual(await b.frame(), (relay.KIND_PEER, b"\x01"))
        self.assertEqual(await a.frame(), (relay.KIND_PEER, b"\x01"))
        await a.close()
        await b.close()

    async def test_messages_are_forwarded_to_the_partner_only(self):
        a = await Client.join(self.port)
        await a.frame()
        b = await Client.join(self.port)
        await b.frame()
        await b.frame()
        await a.frame()

        await a.send(b"\x01ping")
        self.assertEqual(await b.frame(), (relay.KIND_DATA, b"\x01ping"))
        await b.send(b"\x02pong")
        self.assertEqual(await a.frame(), (relay.KIND_DATA, b"\x02pong"))
        await a.close()
        await b.close()

    async def test_rooms_are_separate_and_hold_two_players(self):
        a = await Client.join(self.port, room=b"one")
        b = await Client.join(self.port, room=b"one")
        other = await Client.join(self.port, room=b"two")
        self.assertEqual((await other.frame())[0], relay.KIND_WELCOME)

        third = await Client.join(self.port, room=b"one")
        kind, body = await third.frame()
        self.assertEqual(kind, relay.KIND_ERROR)
        self.assertIn(b"full", body)
        for c in (a, b, other, third):
            await c.close()

    async def test_partner_leaving_is_reported_and_slot_is_reused(self):
        a = await Client.join(self.port)
        await a.frame()
        b = await Client.join(self.port)
        await b.frame()
        await b.frame()
        await a.frame()

        await a.close()
        self.assertEqual(await b.frame(), (relay.KIND_PEER, b"\x00"))

        c = await Client.join(self.port)
        kind, body = await c.frame()
        self.assertEqual((kind, body[0]), (relay.KIND_WELCOME, 0))
        self.assertEqual(await b.frame(), (relay.KIND_PEER, b"\x01"))
        await b.close()
        await c.close()

    async def test_wrong_version_is_refused(self):
        a = await Client.join(self.port, version=99)
        kind, body = await a.frame()
        self.assertEqual(kind, relay.KIND_ERROR)
        self.assertIn(b"version", body)
        await a.close()

    def test_daily_seed_changes_each_day(self):
        self.assertNotEqual(relay.daily_seed(TODAY), relay.daily_seed(TODAY + datetime.timedelta(days=1)))
        self.assertEqual(relay.daily_seed(TODAY), relay.daily_seed(datetime.date(2026, 10, 15)))


if __name__ == "__main__":
    unittest.main()
