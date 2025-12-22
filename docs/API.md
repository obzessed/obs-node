# OBS Node.js Plugin - API Documentation

The OBS Node.js Plugin exposes the OBS Studio API through a global `obs` object.

## Table of Contents

- [Getting Started](#getting-started)
- [obs.version](#obsversion)
- [obs.sources](#obssources)
- [obs.scenes](#obsscenes)
- [obs.sceneItems](#obssceneitems)
- [obs.filters](#obsfilters)
- [obs.transitions](#obstransitions)
- [obs.frontend](#obsfrontend)
- [obs.audio](#obsaudio)
- [obs.events](#obsevents)
- [Examples](#examples)

---

## Getting Started

### Script Editor
**Tools > Node Script Editor** - Write and run scripts in OBS.

| Shortcut | Action |
|----------|--------|
| `Ctrl+Enter` | Run script |
| `Ctrl+S` | Save |
| `Ctrl+N` | New script |
| `Ctrl+O` | Import |
| `Ctrl+E` | Export |
| `Ctrl+D` | Duplicate |
| `F2` | Rename |
| `Del` | Delete (list focused) |
| `Ctrl+Scroll` | Zoom |

### REPL Console
**View > Docks > Node REPL** - Interactive JavaScript console.

---

## obs.version

```javascript
obs.version.string  // "31.0.0"
obs.version.major   // 31
obs.version.minor   // 0
obs.version.patch   // 0
```

---

## obs.sources

```javascript
// List all sources
obs.sources.list()  // ["Desktop Audio", "Webcam", ...]

// Get source info
obs.sources.get("Webcam")  
// { name: "Webcam", id: "dshow_input", width: 1920, height: 1080 }

// Get available source types
obs.sources.getTypes()  // ["image_source", "browser_source", ...]

// Create a source
obs.sources.create("My Color", "color_source", {
  color: 0xFF0000FF,
  width: 1920,
  height: 1080
})

// Remove a source
obs.sources.remove("My Color")
```

---

## obs.scenes

```javascript
// List scenes
obs.scenes.list()  // ["Scene 1", "Game Scene", "BRB"]

// Get scene info
obs.scenes.get("Scene 1")  
// { name: "Scene 1", sources: ["Webcam", "Game Capture"] }

// Create/remove scenes
obs.scenes.create("Stream Scene")
obs.scenes.remove("Old Scene")
```

---

## obs.sceneItems

```javascript
// List items in a scene
obs.sceneItems.list("Scene 1")
// [{ name: "Webcam", id: 1, visible: true, locked: false }, ...]

// Add/remove items
obs.sceneItems.add("Scene 1", "Webcam")
obs.sceneItems.remove("Scene 1", "Webcam")

// Show/hide
obs.sceneItems.setVisible("Scene 1", "Webcam", false)

// Transform
obs.sceneItems.setTransform("Scene 1", "Webcam", {
  posX: 100, posY: 50,
  rotation: 0,
  scaleX: 0.5, scaleY: 0.5
})
```

---

## obs.filters

```javascript
// List filters on a source
obs.filters.list("Webcam")
// [{ name: "Color Correction", id: "color_filter", enabled: true }]

// Get filter types
obs.filters.getTypes()  // ["color_filter", "chroma_key_filter", ...]

// Add a filter
obs.filters.add("Webcam", "My Chroma", "chroma_key_filter")

// Remove a filter
obs.filters.remove("Webcam", "My Chroma")

// Enable/disable
obs.filters.setEnabled("Webcam", "Color Correction", false)

// Get settings (optional: include defaults)
obs.filters.getSettings("Webcam", "Color Correction")
// { brightness: 0.1, contrast: 0.05, ... }

obs.filters.getSettings("Webcam", "Color Correction", true)  // with defaults

// Set settings (merges, doesn't replace all)
obs.filters.setSettings("Webcam", "Color Correction", {
  brightness: 0.2,
  contrast: 0.1
})
```

---

## obs.transitions

```javascript
// List transitions
obs.transitions.list()  // ["Fade", "Cut", "Stinger"]

// Get/set current
obs.transitions.getCurrent()  // "Fade"
obs.transitions.setCurrent("Stinger")

// Duration (ms)
obs.transitions.getDuration()  // 300
obs.transitions.setDuration(500)
```

---

## obs.frontend

### Scene Control

```javascript
obs.frontend.getCurrentScene()  // "Scene 1"
obs.frontend.setCurrentScene("Scene 2")

// Studio mode preview
obs.frontend.getPreviewScene()
obs.frontend.setPreviewScene("Scene 3")
obs.frontend.isStudioMode()
obs.frontend.setStudioMode(true)
```

### Streaming

```javascript
obs.frontend.streaming.start()
obs.frontend.streaming.stop()
obs.frontend.streaming.isActive()
```

### Recording

```javascript
obs.frontend.recording.start()
obs.frontend.recording.stop()
obs.frontend.recording.pause()
obs.frontend.recording.unpause()
obs.frontend.recording.isActive()
obs.frontend.recording.isPaused()
```

### Virtual Camera

```javascript
obs.frontend.virtualCam.start()
obs.frontend.virtualCam.stop()
obs.frontend.virtualCam.isActive()
```

### Replay Buffer

```javascript
obs.frontend.replay.start()
obs.frontend.replay.stop()
obs.frontend.replay.save()  // Save last replay
obs.frontend.replay.isActive()
```

---

## obs.audio

```javascript
// Volume (0.0 to 1.0+)
obs.audio.getVolume("Desktop Audio")
obs.audio.setVolume("Desktop Audio", 1.0)

// Volume in dB
obs.audio.getVolumeDb("Desktop Audio")
obs.audio.setVolumeDb("Desktop Audio", 0)

// Mute
obs.audio.isMuted("Mic/Aux")
obs.audio.setMuted("Mic/Aux", true)

// Monitoring: "none" | "monitorOnly" | "monitorAndOutput"
obs.audio.getMonitorType("Desktop Audio")
obs.audio.setMonitorType("Desktop Audio", "monitorAndOutput")
```

---

## obs.events

```javascript
obs.events.on("SceneChanged", (data) => {
  console.log("Switched to:", data.sceneName)
})

obs.events.on("StreamStarted", () => console.log("Live!"))
obs.events.on("StreamStopped", () => console.log("Ended"))

// Events: SceneChanged, StreamStarted, StreamStopped,
// RecordingStarted, RecordingStopped, RecordingPaused,
// ReplayBufferSaved, VirtualCamStarted, VirtualCamStopped,
// SourceCreated, SourceDestroyed, SourceRenamed, ...
```

---

## Examples

### Toggle Source on Scene Change

```javascript
obs.events.on("SceneChanged", (data) => {
  if (data.sceneName === "Gaming") {
    obs.sceneItems.setVisible("Gaming", "Webcam", true)
  }
})
```

### Dynamic Filter Control

```javascript
obs.events.on("StreamStarted", () => {
  obs.filters.setSettings("Webcam", "Color Correction", {
    saturation: -1.0  // Black & white
  })
})

obs.events.on("StreamStopped", () => {
  obs.filters.setSettings("Webcam", "Color Correction", {
    saturation: 0.0  // Restore color
  })
})
```

### List All Filters

```javascript
for (const src of obs.sources.list()) {
  const filters = obs.filters.list(src)
  if (filters.length) {
    console.log(`\n${src}:`)
    filters.forEach(f => {
      console.log(`  ${f.enabled ? "✓" : "✗"} ${f.name}`)
    })
  }
}
```

---

## TypeScript

Type definitions: `typings/obs.d.ts`

```typescript
const settings = obs.filters.getSettings<ColorCorrectionSettings>(
  "Webcam", "Color Correction"
)
```

---

## Notes

- All API calls are synchronous
- Use `setTimeout` or `async/await` for long operations
- Source/filter names are case-sensitive
