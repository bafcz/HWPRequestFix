require 'fileutils'

MAX_SEARCH = 0x10000
PAIR_DISTANCE = 0x68
GENERAL = ['0fb6f0c1e6184889d048c1e00809de'].pack('H*')
CORE = ['4509c14909c141c1e2184809cb83fe01'].pack('H*')

def segments(path)
  data = File.binread(path)
  abort "#{path}: expected a 64-bit Mach-O" unless data.unpack('V2') == [0xfeedfacf, 0x01000007]
  result = []
  position = 32
  data[16, 4].unpack1('V').times do
    command, size = data[position, 8].unpack('V2')
    abort "#{path}: malformed Mach-O command" if size < 8 || position + size > data.size
    if command == 0x19
      result << data[position + 24, 32].unpack('Q<4')
    end
    position += size
  end
  [data, result]
end

def symbol_address(path, symbol)
  line = IO.popen(['nm', '-n', path], &:read).lines.find do |entry|
    entry.match?(/\A[0-9a-f]{16} \S+ #{Regexp.escape(symbol)}\s*\z/)
  end
  abort "#{path}: missing #{symbol}" unless line
  line.split.first.to_i(16)
end

def unique_pair(window)
  hits = []
  0.upto(window.bytesize - GENERAL.bytesize) do |offset|
    next unless window[offset, GENERAL.bytesize] == GENERAL
    core_offset = offset + PAIR_DISTANCE
    hits << [offset, core_offset] if window[core_offset, CORE.bytesize] == CORE
  end
  abort 'no unique HWP request hook pair found' unless hits.length == 1
  hits.first
end

def verify(path, label)
  data, image_segments = segments(path)
  deadline = symbol_address(path, '_xcpm_get_deadline')
  start = deadline - MAX_SEARCH
  segment = image_segments.find do |vm, vmsize, fileoff, filesize|
    start >= vm && deadline <= vm + vmsize && start >= vm && deadline <= vm + filesize
  end
  abort "#{label}: symbol search range is not file-backed" unless segment
  vm, _, fileoff, = segment
  window = data[fileoff + start - vm, MAX_SEARCH]
  abort "#{label}: incomplete symbol search range" unless window&.bytesize == MAX_SEARCH
  general, core = unique_pair(window)
  general_address = start + general
  core_address = start + core

  starts = {}
  interiors = [(general_address + 1)...(general_address + GENERAL.bytesize),
               (core_address + 1)...(core_address + CORE.bytesize)]
  IO.popen(['otool', '-tvV', path]) do |stream|
    stream.each_line do |line|
      instruction = line.match(/^([0-9a-f]{16})\s+(\S+)\s*(.*)/)
      next unless instruction
      pc = instruction[1].to_i(16)
      starts[pc] = true if pc >= general_address && pc <= core_address + CORE.bytesize
      next unless instruction[2].match?(/\A(j|call|loop)/)
      instruction[3].scan(/0x([0-9a-f]{16})/).each do |target|
        abort "#{label}: incoming branch into displaced instruction: #{line}" if
          interiors.any? { |range| range.cover?(target[0].to_i(16)) }
      end
    end
  end
  if starts.empty? && label == 'BootKC'
    puts "PASS: #{label} unique paired hooks at #{general_address.to_s(16)} / #{core_address.to_s(16)}; embedded disassembly unavailable."
    return
  end
  [general_address, core_address].zip([[0, 3, 6, 9, 13, 15], [0, 3, 6, 10, 13, 16]]).each do |base, offsets|
    offsets.each do |offset|
      abort "#{label}: missing instruction boundary at +#{offset}" unless starts[base + offset]
    end
  end
  puts "PASS: #{label} unique paired hooks at #{general_address.to_s(16)} / #{core_address.to_s(16)}; instruction boundaries and branch scan."
end

verify(ENV.fetch('HWP_KERNEL', '/System/Library/Kernels/kernel'), 'kernel')
verify(ENV.fetch('HWP_BOOTKC', '/System/Library/KernelCollections/BootKernelExtensions.kc'), 'BootKC')
