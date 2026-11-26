# Local adapter for the multi-document YAML capture; not a can_sandbox ROS backend.
# Original timestamps/flags are retained in a separate local TSV, not fabricated in OUT.
require 'yaml'
count = 0
File.open(ARGV.fetch(1), 'w') do |out|
  File.open(ARGV.fetch(2), 'w') do |meta|
    File.read(ARGV.fetch(0)).split(/^---\s*$\n?/).each do |document|
      next if document.strip.empty?
      object = YAML.safe_load(document)
      object.fetch('messages').each do |m|
        bytes = m.fetch('data')
        id = m.fetch('id')
        raise 'invalid frame' unless id.is_a?(Integer) && id.between?(0, 0x1fffffff) &&
          m.fetch('nbytes').between?(0, 8) && bytes.length >= m.fetch('nbytes') && bytes.length <= 8 &&
          bytes.all? { |v| v.is_a?(Integer) && v.between?(0, 255) }
        bytes = bytes.first(m.fetch('nbytes')) # ROS has a fixed-capacity data array.
        out.printf("<0x%08X> [%d] %s\n", id, bytes.length, bytes.map { |v| '%02X' % v }.join(' '))
        time = m.fetch('time')
        meta.puts([count, time.fetch('secs'), time.fetch('nsecs'), m.fetch('bus'), m.fetch('status'), m.fetch('flag')].join("\t"))
        count += 1
      end
    end
  end
end
warn "converted_messages=#{count}"
