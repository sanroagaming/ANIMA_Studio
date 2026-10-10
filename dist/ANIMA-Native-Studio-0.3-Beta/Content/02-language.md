# Language reference
> Program structure, nodes, properties, and expressions.

## Program structure

```anima
app Example {
  state title = "Hello";

  scene Home {
    node Screen {
      flow: column(cross: center, route: center, gap: 16);
      node Title: Heading {
        text: title;
        motion: Reveal;
      }
    }
  }
}
```

### Core declarations

- `app Name { ... }` — top-level program declaration.
- `state name = value;` — declares a state value. General reactive state mutation is not implemented in the included browser runtime.
- `scene Name { ... }` — a named scene that can be navigated to by name.
- `node Name: Type { ... }` — a named element with properties and signals. A node without a type uses the default element behavior.

## Expressions and values

The current language supports common literal values such as quoted strings and numeric values, identifiers, and call-shaped expressions used by built-in actions. A property generally has the form `property: value;`. Keep a semicolon at the end of declarations and property assignments.

```anima
node HeroTitle: Heading {
  text: "A new scene";
  scale: 32;
  weight: 700;
  ink: "#F7F8FA";
}
```

## Property families

- **Content:** `text`, `class`, `mask`.
- **Color and effects:** `fill`, `ink`, `gradient`, `edge`, `halo`, `veil`, `backdrop`, `curve`, `filter`.
- **Typography:** `scale`, `weight`, `voice`, `style`, `tracking`, `leading`, `anchor`, `case`.
- **Sizing:** `wide`, `tall`, `cap`, `minTall`, `maxTall`, `aspect`.
- **Layout and position:** `flow`, `inset`, `offset`, `place`, `top`, `left`, `right`, `bottom`, `gap`, `flex`, `order`, `alignSelf`, `layer`, `clip`, `pointerEvents`, `transform`.
- **Background image:** `bgImage`, `bgSize`, `bgPos`, `bgRepeat`.

These names describe the included runtime's property surface; not every possible CSS property is automatically supported.

## Mounting in the browser host

```js
const program = Anima.parse(source);
const mounted = Anima.mount(program, document.getElementById("app"));
```

This code belongs to the browser host track. The current native C++ front end only loads and structurally checks the room example; it is not yet a full native `Anima.parse`/`Anima.mount` equivalent.

## Implementation notes

The browser runtime supports `app`, `state`, `scene`, `node`, a mapped set of properties, flex-like flow, event handlers, five built-in motions, `go(...)`, `chime(...)`, and the optional `gpu` capability bridge. General loops, timeline blocks, and unrestricted reactive updates are not implemented in this version.


## ANIMA Native 0.2 Preview

The native-first dialect now uses `.ani` source files with a required `ANIMA 0.2` header. Its own vocabulary is `project`, `define`, `start`, `let`, `announce`, `perform`, `repeat`, and `when / otherwise`. It is not translated to C++ source. `AnimaLang.exe` is a native C++20 executable that contains ANIMA's direct reader/evaluator.

```anima
ANIMA 0.2
project "My Native Program" {
    target native.windows
    capability cpu.parallel
    capability gpu.compute
}

define report_hardware {
    announce cpu.logical_cores()
    announce gpu.adapter()
}

start {
    perform report_hardware
    let total: whole <- cpu.parallel_sum(1000000)
    announce total
    when gpu.available() {
        announce gpu.squares(8)
    } otherwise {
        announce "No D3D12 GPU found."
    }
}
```

The old `app { scene { node { ... } } }` syntax remains the browser/legacy track. The 0.2 native dialect is a deliberate new version boundary rather than a claim that every old browser property is already implemented natively.
