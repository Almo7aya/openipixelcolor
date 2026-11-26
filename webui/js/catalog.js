// Names of what the firmware can show, in its own order (firmware/src/app/effects.c, clockface.c and
// clockface2.c). New ones go at the end: the panel saves the number.
export const EFFECTS = ["Plasma", "Fire", "Matrix rain", "Starfield", "Game of Life", "Fireworks", "Ripples", "Sparkles",
  "Rainbow swirl", "Tunnel", "Snake", "Pong", "Bouncing balls", "Equalizer",
  "Aurora", "Lava lamp", "Rainstorm", "DNA helix", "Warp speed", "Radar",
  "Rotozoom", "Twister", "Copper bars", "Shadebobs", "Kaleidoscope", "Julia fractal", "Moiré", "Sunburst",
  "Spirograph", "3D cube", "Synthwave", "Terrain flyover", "Plasma globe", "Tartan",
  "Ocean", "Falling sand", "Langton's ants", "Flock", "Galaxy", "Snowfall", "Aquarium", "Fireflies",
  "Maze", "Sorting", "Pendulum wave", "Brick breaker", "Meadow"];
export const CLOCKS = ["Digital with date", "Analog", "Binary", "Big digits", "Digits on plasma",
  "Flip cards", "Nixie tubes", "Rings", "Day and night sky", "Pong clock",
  "Words", "Berlin clock", "Fibonacci", "Morphing digits", "Hourglass", "Falling bricks", "Matrix digits",
  "Fire digits", "LCD watch", "Neon sign", "Dice", "Moon phase", "Orbit", "Progress", "Colour code", "Roman numerals"];
export const MODES = ["Effect", "Clock", "Text", "Picture or drawing", "Equalizer", "Scoreboard", "Stopwatch",
  "Test pattern", "Starting up", "Animation", "Welcome screen"];
export const TESTS = ["Orientation marker", "Colour bars", "Brightness ramps", "Checkerboard", "Row sweep", "Column sweep", "All white"];
export const fxIndex = (name) => Math.max(0, EFFECTS.indexOf(name));
export const previewOf = (kind, i) => `previews/${kind}-${String(i).padStart(2, "0")}`;
