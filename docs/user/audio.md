# Sound

A game declares its mixer and its sounds as documents, and entities play them through emitter components. A dedicated server carries the components as plain values and plays nothing. How it works is in [audio internals](../internals/audio.md).

## The mixer

A game's `mixer` line names an `audio.mixer` document: one tree of buses from the master, each with a volume, effects (dynamics, reverb and others) and sends to other buses, and the concurrency sets sounds share voices in.

```json
{
  "kind": "audio.mixer",
  "formatVersion": 1,
  "master": {
    "busId": "b64c164e5d653e5c", "name": "master", "role": "master",
    "effects": [ { "type": "rawframe/dynamics@1", "processor": "limiter", "threshold": -1, "attack": 0.001 } ],
    "children": [
      { "busId": "72d352d73ed19e64", "name": "music", "role": "music", "volume": -12 },
      { "busId": "691c5abeb88d55ee", "name": "effects", "role": "sfx", "volume": -6 }
    ]
  }
}
```

A bus's `role` is what a player's volume settings bind to, never its name.

## Sounds

Each `sound <16 hex digits> <file>` line declares an `audio.sound` document: its variants (the clips, by resource), the ranges each play draws its volume and pitch from, whether it loops, the bus it routes to, its concurrency set, and how it fades with distance.

```json
{
  "kind": "audio.sound",
  "formatVersion": 1,
  "variants": [ { "resource": "1274499474142e189b67a3b499591324" } ],
  "pitch": { "minimum": 0.95, "maximum": 1.05 },
  "bus": "691c5abeb88d55ee",
  "concurrency": "jumps",
  "attenuation": { "minimumDistance": 2, "maximumDistance": 30, "falloff": "linear" }
}
```

Clips are WAVE, Ogg Vorbis or MP3 files the cook turns into short clips or Opus, as each clip's sidecar `tier` says; long sounds stream.

## Playing

- An entity with a `rawframe.sound.Emitter` component plays its declared sounds from where its pose puts it, heard by the client's player. Present systems play sounds through emitters of their own, so a sound is presentation and never reaches the server.
- A predicted effect may name a sound (`effect jump predicted sound <id>`), which the player hears at once and once.
- A sound with no voice free goes virtual rather than silent, and comes back where it would be when one frees.

A client plays sound when `audio.play` is set; exported games set it. `audio.record` records what a client heard to a file instead, for tests.

Sound plays on the system's default output and follows it: plugging in headphones that become the default moves it there. `audio.device` in the client's configuration on the player's machine names another device by its key, which stays the same when the device is unplugged and plugged again; while that device is away the default plays, and sound returns to it when it comes back. Each move is logged (`audio.output_moved`).
