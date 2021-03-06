type address = int;
type byte = int;

type t = {
  get_prg: address => byte,
  set_prg: (address, byte) => unit,
  get_chr: address => byte,
  set_chr: (address, byte) => unit,
  mirroring: unit => Rom.mirroring,
  set_mirroring: Rom.mirroring => unit,
};

exception NotImplemented(Rom.mapper);
exception NotAllowed(string);

let nrom = (rom: Rom.t): t => {
  let mirroring = ref(rom.mirroring);

  {
    get_prg: address => Util.aref(rom.prg, address land (rom.prg_size - 1)),
    set_prg: (_, _) => raise(NotAllowed("Cannot write to prg")),
    get_chr: address => Util.aref(rom.chr, address),
    set_chr: (address, byte) => Util.set(rom.chr, address, byte),
    mirroring: () => mirroring^,
    set_mirroring: style => mirroring := style,
  };
};

let unrom = (rom: Rom.t): t => {
  let mirroring = ref(rom.mirroring);
  let prg_bank = ref(0);

  let get_prg = address => {
    let bank = address < 0xc000 ? prg_bank^ : rom.prg_count - 1;
    Util.aref(rom.prg, bank * 0x4000 + address land 0x3fff);
  };
  let set_prg = (_, value) => prg_bank := value land 0b111;

  let get_chr = address => Util.aref(rom.chr, address);
  let set_chr = (address, byte) => Util.set(rom.chr, address, byte);

  {
    get_prg,
    set_prg,
    get_chr,
    set_chr,
    mirroring: () => mirroring^,
    set_mirroring: style => mirroring := style,
  };
};

let cnrom = (rom: Rom.t): t => {
  let mirroring = ref(rom.mirroring);
  let chr_bank = ref(0);

  let chr_addr = offset => chr_bank^ * 0x2000 + offset;

  let get_prg = address =>
    Util.aref(rom.prg, address land (rom.prg_size - 1));
  let set_prg = (_, value) => chr_bank := value land 0b11;

  let get_chr = address => Util.aref(rom.chr, chr_addr(address));
  let set_chr = (address, byte) =>
    Util.set(rom.chr, chr_addr(address), byte);

  {
    get_prg,
    set_prg,
    get_chr,
    set_chr,
    mirroring: () => mirroring^,
    set_mirroring: style => mirroring := style,
  };
};