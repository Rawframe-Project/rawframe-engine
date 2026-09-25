"""A browser's side of WebTransport, played by aioquic, an independent
HTTP/3 implementation: it opens a session on the server at the port given,
sends on a two-way stream and as a datagram, and waits for the server's
answers on that stream, on a one-way stream of the server's, and as a
datagram. Prints `webtransport: answered` and exits 0 when all three came.

usage: webtransport_client.py <port>
"""
import asyncio
import ssl
import sys

from aioquic.asyncio.client import connect
from aioquic.asyncio.protocol import QuicConnectionProtocol
from aioquic.h3.connection import H3_ALPN, FrameType, H3Connection
from aioquic.h3.events import DatagramReceived, HeadersReceived, WebTransportStreamDataReceived
from aioquic.quic.configuration import QuicConfiguration


class Browser(QuicConnectionProtocol):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.http = H3Connection(self._quic, enable_webtransport=True)
        self.status = asyncio.get_event_loop().create_future()
        self.answers = {}
        self.all_answered = asyncio.Event()

    def quic_event_received(self, event):
        for http_event in self.http.handle_event(event):
            if isinstance(http_event, HeadersReceived) and not self.status.done():
                self.status.set_result(dict(http_event.headers).get(b":status"))
            elif isinstance(http_event, WebTransportStreamDataReceived):
                key = "two-way" if http_event.stream_id % 4 == 0 else "one-way"
                self.answers[key] = self.answers.get(key, b"") + http_event.data
            elif isinstance(http_event, DatagramReceived):
                self.answers["datagram"] = http_event.data
        if (
            self.answers.get("two-way") == b"pong"
            and self.answers.get("one-way") == b"hello"
            and self.answers.get("datagram") == b"back"
        ):
            self.all_answered.set()


async def main(port):
    configuration = QuicConfiguration(is_client=True, alpn_protocols=H3_ALPN, max_datagram_frame_size=65536)
    # The server's identity is pinned by hash in a browser; here it is not
    # what is under test.
    configuration.verify_mode = ssl.CERT_NONE
    async with connect("127.0.0.1", port, configuration=configuration, create_protocol=Browser) as browser:
        session = browser._quic.get_next_available_stream_id()
        browser.http.send_headers(
            stream_id=session,
            headers=[
                (b":method", b"CONNECT"),
                (b":protocol", b"webtransport"),
                (b":scheme", b"https"),
                (b":authority", f"127.0.0.1:{port}".encode()),
                (b":path", b"/rawframe"),
            ],
        )
        browser.transmit()
        status = await asyncio.wait_for(browser.status, 5)
        if status != b"200":
            print(f"webtransport: refused with {status}")
            return 1
        stream = browser.http.create_webtransport_stream(session)
        # aioquic 0.9 does not mark a two-way stream it opened as the
        # session's, so it would read the answer as HTTP/3 frames; a browser
        # reads it as the stream's bytes, and so must this.
        opened = browser.http._get_or_create_stream(stream)
        opened.frame_type = FrameType.WEBTRANSPORT_STREAM
        opened.session_id = session
        browser._quic.send_stream_data(stream, b"ping")
        browser.http.send_datagram(session, b"dgram")
        browser.transmit()
        try:
            await asyncio.wait_for(browser.all_answered.wait(), 5)
        except TimeoutError:
            print(f"webtransport: only {browser.answers}")
            return 1
        print("webtransport: answered")
        return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main(int(sys.argv[1]))))
