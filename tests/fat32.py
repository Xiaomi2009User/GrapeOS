"""Read a FAT32 image directly, independently of the guest's UEFI file API."""
import struct

class Volume:
    def __init__(self, disk):
        self.disk = disk
        entries = struct.unpack_from('<Q', disk, 512 + 72)[0]
        start = struct.unpack_from('<Q', disk, entries * 512 + 32)[0]
        self.base = start * 512
        self.sector = struct.unpack_from('<H', disk, self.base + 11)[0]
        self.cluster_size = disk[self.base + 13] * self.sector
        reserved = struct.unpack_from('<H', disk, self.base + 14)[0]
        fats = disk[self.base + 16]
        fat_sectors = struct.unpack_from('<I', disk, self.base + 36)[0]
        self.root = struct.unpack_from('<I', disk, self.base + 44)[0]
        self.fat = self.base + reserved * self.sector
        self.data = self.fat + fats * fat_sectors * self.sector
        # FAT mirrors must agree after the guest has flushed its files.
        if fats == 2:
            count = fat_sectors * self.sector
            assert disk[self.fat:self.fat+count] == disk[self.fat+count:self.fat+2*count], 'FAT copies disagree'
    def chain(self, first):
        output = bytearray(); visited = set()
        while 2 <= first < 0x0ffffff8:
            assert first not in visited, 'FAT chain loop'
            visited.add(first)
            offset = self.data + (first - 2) * self.cluster_size
            output.extend(self.disk[offset:offset+self.cluster_size])
            first = struct.unpack_from('<I', self.disk, self.fat + first * 4)[0] & 0x0fffffff
        return bytes(output)
    def directory(self, cluster):
        entries = {}; long_parts = {}
        data = self.chain(cluster)
        for offset in range(0, len(data), 32):
            record = data[offset:offset+32]
            if not record[0]: break
            if record[0] == 0xe5: long_parts.clear(); continue
            if record[11] == 0x0f:
                raw = record[1:11] + record[14:26] + record[28:32]
                text = ''.join(chr(c) for (c,) in struct.iter_unpack('<H', raw) if c not in (0, 0xffff))
                long_parts[record[0] & 31] = text
                continue
            if record[11] & 8: continue
            if long_parts:
                name = ''.join(long_parts[i] for i in sorted(long_parts)); long_parts.clear()
            else:
                main = record[:8].decode('ascii').rstrip(); ext = record[8:11].decode('ascii').rstrip()
                name = main + ('.' + ext if ext else '')
            first = (struct.unpack_from('<H', record, 20)[0] << 16) | struct.unpack_from('<H', record, 26)[0]
            length = struct.unpack_from('<I', record, 28)[0]
            entries[name.lower()] = (first, length, bool(record[11] & 16))
        return entries
    def read(self, path):
        current = self.root
        for index, part in enumerate(path.strip('/').split('/')):
            first, length, directory = self.directory(current)[part.lower()]
            if directory: current = first
            else: return self.chain(first)[:length]
        return self.directory(current)
