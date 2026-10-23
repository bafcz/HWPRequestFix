require 'open3'
require 'set'

def symbols(path, flags)
  output, status = Open3.capture2('nm', flags, path)
  abort "nm failed: #{path}" unless status.success?
  output.lines.map(&:strip).reject(&:empty?).to_set
end

kext = 'Build/HWPRequestFix.kext/Contents/MacOS/HWPRequestFix'
lilu = ENV.fetch('HWP_LILU_BINARY')
needed = symbols(kext, '-uj')
available = symbols('/System/Library/Kernels/kernel', '-gUj') | symbols(lilu, '-gUj')
missing = needed - available
# Legacy IOKit vtable slots are repaired by the kext linker. The active Lilu
# has exactly these same two inherited IOService references on this kernel.
legacy_slots = Set.new(%w[__ZN9IOService19_RESERVEDIOService0Ev __ZN9IOService19_RESERVEDIOService1Ev])
permitted_slots = legacy_slots & symbols(lilu, '-uj')
unexpected = missing - permitted_slots
abort "Unresolved imports: #{unexpected.to_a.join(', ')}" unless unexpected.empty?
abort 'Unexpected floating-point/SIMD kernel code' if Open3.capture2('otool', '-tvV', kext)[0].match?(/%(xmm|ymm|zmm|mm)[0-9]/)
puts "PASS: #{(needed - missing).size} imports resolved directly; #{missing.size} legacy IOKit vtable slots also used by active Lilu; no SIMD instructions."
puts "Verified Lilu dependency: #{lilu}"
