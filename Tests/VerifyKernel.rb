require 'digest'
require 'fileutils'

kernel = ENV.fetch('HWP_KERNEL', '/System/Library/Kernels/kernel')
expected_hash = '7004ebc1020a5c7e3f075c631507f31fdc0ce3fe02e9f01f42dce1d178770a08'
abort 'Unverified kernel: refusing to build' unless Digest::SHA256.file(kernel).hexdigest == expected_hash
data = File.binread(kernel)
abort 'Not a thin x86_64 Mach-O' unless data.unpack('V2') == [0xfeedfacf, 0x01000007]
segments = []
position = 32
uuid = nil
data[16, 4].unpack1('V').times do
  command, size = data[position, 8].unpack('V2')
  abort 'Bad Mach-O command' if size < 8
  if command == 0x19
    vmaddr, vmsize, fileoff, filesize = data[position + 24, 32].unpack('Q<4')
    segments << [vmaddr, vmsize, fileoff, filesize]
  elsif command == 0x1b
    uuid = data[position + 8, 16].unpack1('H*')
  end
  position += size
end
abort 'UUID mismatch' unless uuid == '1291d44ac0cd3ac7a9e4ca2749b33959'
address = 0xffffff8000406440
segment = segments.find { |vm, _, _, size| address >= vm && address + 0x420 <= vm + size }
abort 'Cannot map request function' unless segment
offset = address - segment[0] + segment[2]
signature = data[offset, 0x420]
abort 'General site mismatch' unless signature[0x80, 15].unpack1('H*') == '0fb6f0c1e6184889d048c1e00809de'
abort 'Core site mismatch' unless signature[0xe8, 16].unpack1('H*') == '4509c14909c141c1e2184809cb83fe01'

interiors = [(address + 0x81)...(address + 0x8f), (address + 0xe9)...(address + 0xf8)]
instruction_starts = {}
IO.popen(['otool', '-tvV', kernel]) do |stream|
  stream.each_line do |line|
    instruction = line.match(/^([0-9a-f]{16})\s+(\S+)\s*(.*)/)
    next unless instruction
    pc = instruction[1].to_i(16)
    instruction_starts[pc] = true if pc >= address && pc < address + 0x420
    next unless instruction[2].match?(/\A(j|call|loop)/)
    instruction[3].scan(/0x([0-9a-f]{16})/).each do |target|
      abort "Incoming branch into displaced block: #{line}" if interiors.any? { |range| range.cover?(target[0].to_i(16)) }
    end
  end
end
[0x80, 0x83, 0x86, 0x89, 0x8d, 0x8f, 0xe8, 0xeb, 0xee, 0xf2, 0xf5, 0xf8].each do |relative|
  abort "Missing instruction boundary #{relative.to_s(16)}" unless instruction_starts[address + relative]
end
FileUtils.mkdir_p('Build')
header = "#pragma once\nstatic const unsigned char kernelSignature[] = {\n"
signature.bytes.each_slice(16) { |row| header << '    ' + row.map { |byte| '0x%02x' % byte }.join(', ') + ",\n" }
header << "};\n"

# KC linking changes RIP-relative operands even when the kernel UUID is identical.
kc_path = ENV.fetch('HWP_BOOTKC', '/System/Library/KernelCollections/BootKernelExtensions.kc')
abort 'Unverified BootKC' unless Digest::SHA256.file(kc_path).hexdigest == '21d6da1c4c3e7b72dddf5144da8a4c4b48e294249a16018a5006b61bb2155ccc'
kc = File.binread(kc_path)
kc_segments = []
inner = nil
p = 32
kc[16, 4].unpack1('V').times do
  cmd, size = kc[p, 8].unpack('V2')
  abort 'Invalid KC load command' if size < 8 || p + size > kc.size
  kc_segments << kc[p + 24, 32].unpack('Q<4') if cmd == 0x19
  if cmd == 0x80000035
    name_offset = kc[p + 24, 4].unpack1('V')
    inner = kc[p + 16, 8].unpack1('Q<') if kc[p + name_offset, size - name_offset].split("\0").first == 'com.apple.kernel'
  end
  p += size
end
abort 'No kernel in BootKC' unless inner
p = inner + 32
kc_uuid = nil
kc[inner + 16, 4].unpack1('V').times do
  cmd, size = kc[p, 8].unpack('V2')
  kc_uuid = kc[p + 8, 16].unpack1('H*') if cmd == 0x1b
  p += size
end
abort 'BootKC kernel UUID mismatch' unless kc_uuid == uuid
symbols = IO.popen(['nm', '-n', kc_path], &:read).lines.map do |line|
  m = line.match(/\A([0-9a-f]{16}) \S+ (\S+)\s*\z/)
  [m[2], m[1].to_i(16)] if m
end.compact.to_h
kc_address = symbols.fetch('_xcpm_get_deadline') - 0x420
abort 'BootKC symbol distance changed' unless symbols['_xcpm_get_deadline'] - symbols['_xcpm_pkg_dvfs_init'] == 0x1d00
seg = kc_segments.find { |vm, _, _, size| kc_address >= vm && kc_address + 0x420 <= vm + size }
abort 'Cannot map BootKC helper' unless seg
kc_signature = kc[kc_address - seg[0] + seg[2], 0x420]
abort 'BootKC general hook changed' unless kc_signature[0x80, 15] == signature[0x80, 15]
abort 'BootKC core hook changed' unless kc_signature[0xe8, 16] == signature[0xe8, 16]
kc_interiors = [(kc_address + 0x81)...(kc_address + 0x8f), (kc_address + 0xe9)...(kc_address + 0xf8)]
IO.popen(['otool', '-tvV', kc_path]) do |stream|
  stream.each_line do |line|
    insn = line.match(/^([0-9a-f]{16})\s+(\S+)\s*(.*)/)
    next unless insn && insn[2].match?(/\A(j|call|loop)/)
    insn[3].scan(/0x([0-9a-f]{16})/).each do |target|
      abort "BootKC incoming branch into displaced block: #{line}" if kc_interiors.any? { |r| r.cover?(target[0].to_i(16)) }
    end
  end
end
header << "static const unsigned char bootKCSignature[] = {\n"
kc_signature.bytes.each_slice(16) { |row| header << '    ' + row.map { |byte| '0x%02x' % byte }.join(', ') + ",\n" }
header << "};\nstatic const unsigned char expectedKernelUUID[16] = {\n    "
header << [uuid].pack('H*').bytes.map { |b| '0x%02x' % b }.join(', ') + "\n};\n"
File.write('Build/KernelSignature.hpp', header)
puts "PASS: kernel SHA256, UUID, both instruction blocks, no incoming direct branches into displaced interiors."
puts "PASS: BootKC SHA256, embedded kernel UUID, symbol distances, exact hooks, branch scan and full relocated signature."
