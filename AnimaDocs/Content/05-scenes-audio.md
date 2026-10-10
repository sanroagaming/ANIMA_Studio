# Scenes and audio
> Organize separate views and produce a small synthesized sound.

## Scene navigation

Declare scenes inside an app, then call `go` with the destination scene name.

```anima
app SceneDemo {
  scene Home {
    node Welcome: Heading { text: "Home"; }
    node Next: Button {
      text: "Open details";
      on click: go("Details");
    }
  }

  scene Details {
    node DetailTitle: Heading { text: "Details"; }
    node Back: Button {
      text: "Back home";
      on click: go("Home");
    }
  }
}
```

`go("SceneName")` navigates to a declared scene, according to the included browser runtime.

## Synthesized chimes

```anima
node Chime: Button {
  text: "Play a chime";
  on click: chime(880);
}
```

`chime(880)` synthesizes a short chime at a base frequency of 880 Hz. The browser implementation relies on browser audio capabilities; it isn't a native audio engine in the C++ runtime yet.

## State and reactivity

A source declaration such as `state title = "Hello";` communicates a value used by a program. General reactive state mutation and signal-driven dependency tracking are not available in the included browser runtime. Use the examples as syntax demonstrations, not as a promise of a fully reactive application framework.
