let rom_path = Node.Process.argv[2];

let nes = File.rom(rom_path) |> Nes.load;

let continue = _ => Nes.Continue;

let write: (string, Render.frame) => unit = [%bs.raw
  {|
    function(path, frame) {
      const Jimp = require('jimp');
      new Jimp({
        data: Buffer.from(frame),
        width: 256,
        height: 240,
    }, (err, image) => {
      image.write(path);
    });
   }
|}
];

for (_tick in 0 to 360) {
  ignore(Nes.step_frame(nes, ~on_frame=result => nes.frame = result));
};