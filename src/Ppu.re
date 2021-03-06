type registers = {
  mutable control: int,
  mutable mask: int,
  mutable status: int,
  mutable oam_address: int,
  mutable oam_data: int,
  mutable ppu_address: int,
  mutable ppu_data: int,
  mutable buffer: int,
  mutable fine_x: int,
  mutable write_latch: bool,
};

type vblank_nmi =
  | NMIEnabled
  | NMIDisabled;

type t = {
  registers,
  oam: array(int),
  name_table: array(int),
  palette_table: array(int),
  pattern_table: Mapper.t,
};

let build = (~name_table=Array.make(0x800, 0), mapper) => {
  registers: {
    control: 0,
    mask: 0,
    status: 0,
    oam_address: 0,
    oam_data: 0,
    ppu_address: 0,
    ppu_data: 0,
    buffer: 0,
    fine_x: 0,
    write_latch: false,
  },
  oam: Array.make(0x100, 0),
  name_table,
  palette_table: Array.make(0x20, 0),
  pattern_table: mapper,
};

let ctrl_helper = (n, unset, set, regs) =>
  Util.read_bit(regs.control, n) ? set : unset;

let mask_helper = (n, regs) => Util.read_bit(regs.mask, n);

let status_helper = (n, regs, to_) => {
  regs.status = Util.set_bit(regs.status, n, to_);
};

let x_scroll_offset = ctrl_helper(0, 0, 256);
let y_scroll_offset = ctrl_helper(1, 0, 240);
let vram_step = ctrl_helper(2, 1, 32);
let sprite_address = ctrl_helper(3, 0, 0x1000);
let background_address = ctrl_helper(4, 0, 0x1000);
let vblank_nmi = ctrl_helper(7, NMIDisabled, NMIEnabled);

let sprite_offset = ppu => Util.read_bit(ppu.registers.control, 3) ? 0x1000 : 0;
let background_offset = ppu =>
  Util.read_bit(ppu.registers.control, 4) ? 0x1000 : 0;

let show_background_left = mask_helper(1);
let show_sprites_left = mask_helper(2);
let show_background = mask_helper(3);
let show_sprites = mask_helper(4);

let set_sprite_zero_hit = status_helper(6);
let set_vblank = status_helper(7);
let rendering_enabled = ppu =>
  show_background(ppu.registers) || show_sprites(ppu.registers);
let sprite_zero_enabled = ppu =>
  show_background(ppu.registers) && show_sprites(ppu.registers);

let check_zero_hit = (ppu, sp_pixel, bg_pixel) =>
  if (sprite_zero_enabled(ppu) && sp_pixel > 0 && bg_pixel > 0) {
    set_sprite_zero_hit(ppu.registers, true);
  };

let nt_offset = nt_index => 0x2000 + nt_index * 0x400;
let nt_mirror = (ppu, address) => {
  let mirroring = ppu.pattern_table.mirroring();
  // Bit 11 indicates we're reading from nametables 3 and 4, i.e. 2800 and 2C00.
  // TODO: Support Upper and Lower mirroring.
  switch (mirroring, Util.read_bit(address, 11)) {
  | (Rom.Horizontal, false) => address land 0x3ff
  | (Rom.Horizontal, true) => 0x400 + address land 0x3ff
  | _ => address land 0x7ff // Vertical
  };
};

let palette_mirror = addr => {
  let mirrored = addr land 0x1f;
  mirrored > 0x0f && mirrored mod 4 == 0 ? mirrored - 16 : mirrored;
}

let read_vram = (ppu, address) =>
  if (address < 0x2000) {
    ppu.pattern_table.get_chr(address);
  } else if (address < 0x3f00) {
    let mirrored_addr = nt_mirror(ppu, address);
    ppu.name_table[mirrored_addr];
  } else {
    let mirrored_addr = palette_mirror(address);
    ppu.palette_table[mirrored_addr];
  };

let write_vram = (ppu, value) => {
  let address = ppu.registers.ppu_address;
  if (address < 0x2000) {
    ppu.pattern_table.set_chr(address, value);
  } else if (address < 0x3f00) {
    let mirrored_addr = nt_mirror(ppu, address);
    ppu.name_table[mirrored_addr] = value;
  } else {
    let mirrored_addr = palette_mirror(address);
    ppu.palette_table[mirrored_addr] = value;
  };
  ppu.registers.ppu_address = address + vram_step(ppu.registers);
};

let read_status = ppu => {
  let result = ppu.registers.status;
  ppu.registers.write_latch = false;
  set_vblank(ppu.registers, false);
  result;
};

let read_oam = ppu => {
  ppu.oam[ppu.registers.oam_address];
}

let read_ppu_data = ppu => {
  let address = ppu.registers.ppu_address;
  let buffer = ppu.registers.ppu_data;
  let result = read_vram(ppu, address);
  ppu.registers.ppu_address = (address + vram_step(ppu.registers)) land 0x3fff;
  if (address >= 0x3f00) {
    ppu.registers.ppu_data = ppu.name_table[nt_mirror(ppu, address)];
    result;
  } else {
    ppu.registers.ppu_data = result;
    buffer;
  }
};

let fetch = (ppu: t, address) =>
  switch (address land 7) {
  | 2 => read_status(ppu)
  | 4 => read_oam(ppu)
  | 7 => read_ppu_data(ppu)
  | _ => 0
  };

let write_control = (ppu: t, value) => {
  ppu.registers.control = value;
  let nt_index = (value land 0x3) lsl 10;
  ppu.registers.buffer = ppu.registers.buffer lor nt_index;
};

let write_oam = (ppu: t, value) => {
  let {oam_address} = ppu.registers;
  ppu.oam[oam_address] = value;
  ppu.registers.oam_address = (oam_address + 1) land 0xff;
};

let write_scroll = (ppu: t, value) => {
  let regs = ppu.registers;
  if (regs.write_latch) {
    let coarse_y_bits = (value lsr 3) lsl 5;
    let fine_y_bits = (value land 7) lsl 12;
    regs.buffer = regs.buffer lor coarse_y_bits lor fine_y_bits;
  } else {
    let coarse_x_bits = value lsr 3;
    let fine_x_bits = value land 7;
    regs.buffer = coarse_x_bits;
    regs.fine_x = fine_x_bits;
  };
  regs.write_latch = !regs.write_latch;
};

let write_address = (ppu: t, value) => {
  let regs = ppu.registers;
  if (regs.write_latch) {
    regs.buffer = regs.buffer lor value;
    regs.ppu_address = regs.buffer;
    regs.write_latch = false;
  } else {
    regs.buffer = value lsl 8 land 0x7fff;
    regs.write_latch = true;
  };
};

let store = (ppu: t, address, value) =>
  switch (address land 7) {
  | 0 => write_control(ppu, value)
  | 1 => ppu.registers.mask = value
  | 3 => ppu.registers.oam_address = value
  | 4 => write_oam(ppu, value)
  | 5 => write_scroll(ppu, value)
  | 6 => write_address(ppu, value)
  | 7 => write_vram(ppu, value)
  | _ => ()
  };