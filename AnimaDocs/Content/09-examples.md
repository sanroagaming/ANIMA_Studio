# Examples library
> Small source files you can open, copy, and adapt.

## Hello ANIMA

```anima
app Hello {
  state greeting = "Hello, ANIMA";
  scene Home {
    node Screen {
      flow: column(cross: center, route: center, gap: 16);
      node Title: Heading {
        text: greeting;
        motion: Reveal;
      }
    }
  }
}
```

## Glass card

```anima
app GlassCard {
  scene Home {
    node Screen {
      flow: column(cross: center, gap: 20);
      node Panel {
        fill: "rgba(255,255,255,0.10)";
        edge: "rgba(255,255,255,0.22)";
        curve: 24;
        halo: "rgba(82,140,255,0.28)";
        inset: 24;
        node Label: Heading {
          text: "Glass, composed.";
          scale: 28;
        }
      }
    }
  }
}
```

## Chime

```anima
app ChimeDemo {
  scene Home {
    node Action: Button {
      text: "Play chime";
      on click: chime(880);
    }
  }
}
```

## Browse source files

The `Examples/` folder ships standalone `.anima` files: `hello.anima`, `glass-card.anima`, `chime.anima`, `motion-composition.anima`, `scene-navigation.anima`, `webgpu.anima`, and `raytrace-room.anima`. It also includes a browser mount example and the earlier `webgpu-host.html` / `raytrace-room-host.html` pages. Click **Open example** to open a source file in Notepad, use **Copy** on a code block, or use the GPU page actions to open the optional browser hosts.

## Preview and run

The native documentation app offers a **Run room demo** action to launch the separate native Direct3D 12 room renderer when it has been built. That native renderer is a dedicated demo; it does not yet interpret each sample and render it as an arbitrary app. Browser-host WebGPU examples remain available as source/reference files and need a WebGPU-capable browser if you choose to run those old host pages.


## Native language examples (`.ani`)

The new ANIMA Native 0.2 dialect is independent of the legacy `app / scene / node` source form. Choose **ANIMA Native 0.2** in the sidebar to run the native example or open the source.

- `native-power.ani` — CPU worker threads, Direct3D 12 adapter queries, GPU compute and reusable definitions.
- `definitions.ani` — custom definitions and the `perform` statement.
- `math-and-conditions.ani` — typed values, arithmetic, comparisons and `when / otherwise`.

The source file should begin with `ANIMA 0.2`. See `AnimaLang/docs/ANIMA_02_LANGUAGE_SPEC.md` for syntax and capability limits.
