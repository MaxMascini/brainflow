% Board without usb id matches nothing, real boards return a cell array of names.
ports = BoardShim.get_serial_ports(int32(BoardIds.SYNTHETIC_BOARD));
assert(isempty(ports), 'Synthetic board must not match serial ports');
ports = BoardShim.get_serial_ports(int32(BoardIds.CYTON_BOARD));
assert(iscell(ports), 'Serial ports must be a cell array');
fprintf('Serial ports for Cyton: %d\n', numel(ports));
