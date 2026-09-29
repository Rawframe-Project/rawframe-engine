// A client's sound played by the browser (D259): the frames the client
// rendered, taken a few times a frame's worth at a time and queued on the
// page's AudioContext just ahead of what it plays. The browser plays sound
// on its own thread, which cannot call into the client; the page can.

/** How far ahead of the browser's playing the queue is kept, in seconds. */
const kLead = 0.06;
/** How often the page takes the client's frames, in milliseconds. */
const kEvery = 20;

export class PageSound {
    /**
     * Plays `client`'s sound on a new AudioContext at the client's rate. A
     * browser lets it start only after the player has touched the page:
     * `resume()` from an input handler.
     */
    constructor(client) {
        this.client = client;
        this.rate = client.soundRate();
        this.context = new AudioContext({ sampleRate: this.rate });
        this.next = 0;
        /** Frames taken, and the loudest sample among them. */
        this.frames = 0;
        this.peak = 0;
        this.timer = setInterval(() => this.pump(), kEvery);
    }

    resume() {
        return this.context.resume();
    }

    /** Takes what the client rendered and queues it after what is queued. */
    pump() {
        const samples = this.client.takeSound(Math.ceil((this.rate * 4 * kEvery) / 1000));
        const frames = samples.length / 2;
        if (frames === 0) {
            return;
        }
        this.frames += frames;
        const buffer = this.context.createBuffer(2, frames, this.rate);
        const left = buffer.getChannelData(0);
        const right = buffer.getChannelData(1);
        for (let frame = 0; frame < frames; frame += 1) {
            left[frame] = samples[frame * 2];
            right[frame] = samples[(frame * 2) + 1];
            this.peak = Math.max(this.peak, Math.abs(left[frame]), Math.abs(right[frame]));
        }
        const source = this.context.createBufferSource();
        source.buffer = buffer;
        source.connect(this.context.destination);
        this.next = Math.max(this.next, this.context.currentTime + kLead);
        source.start(this.next);
        this.next += frames / this.rate;
    }

    /** Stops taking and playing. */
    close() {
        clearInterval(this.timer);
        return this.context.close();
    }
}
