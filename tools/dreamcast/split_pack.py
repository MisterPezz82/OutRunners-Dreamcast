# SPDX-License-Identifier: BSD-3-Clause
import sys, struct, hashlib, json
from pathlib import Path

def fail(msg):
    print(f"Error: {msg}", file=sys.stderr)
    sys.exit(1)

def read_u32(data, offset):
    if offset + 4 > len(data): fail("Truncated u32")
    return struct.unpack_from('<I', data, offset)[0]

def read_bytes(data, offset, length):
    if offset + length > len(data): fail("Truncated data")
    return data[offset:offset+length]

def main():
    if len(sys.argv) != 3: fail("Usage: split_pack.py <input> <output_dir>")
    input_path = Path(sys.argv[1])
    output_dir = Path(sys.argv[2])
    
    if not input_path.is_file(): fail("Input not found")
    if output_dir.exists(): fail("Output directory exists")

    data = input_path.read_bytes()
    if len(data) < 12: fail("File too small")
    
    magic = data[:4]
    if magic != b'ORAP': fail("Bad magic")
    version = struct.unpack_from('<I', data, 4)[0]
    table_version = struct.unpack_from('<I', data, 8)[0]
    if version != 1: fail("Unsupported version")

    offset = 12
    max_count = 100000
    
    # Parse two data regions to advance offset
    for _ in range(2):
        count = read_u32(data, offset); offset += 4
        if count > max_count: fail("Region count too large")
        for _ in range(count):
            addr = read_u32(data, offset); offset += 4
            length = read_u32(data, offset); offset += 4
            if length > 10000000: fail("Piece too large")
            read_bytes(data, offset, length)
            offset += length
            pad = (4 - (length % 4)) % 4
            if pad > 0:
                read_bytes(data, offset, pad)
                offset += pad

    core_prefix = data[:offset]

    # Media: Tile Bitmap
    if offset + 4096 > len(data): fail("Missing tile bitmap")
    tile_bitmap = data[offset:offset+4096]
    offset += 4096
    
    # Tiles
    tiles = bytearray(8 * 1024 * 1024) # 8MiB
    tiles[:] = b'\x0f' * len(tiles)
    for i in range(32768):
        if (tile_bitmap[i // 8] >> (i % 8)) & 1:
            tile_data = read_bytes(data, offset, 256)
            offset += 256
            tiles[i*256:(i+1)*256] = tile_data

    # Sprites
    sprite_count = read_u32(data, offset); offset += 4
    if sprite_count > max_count: fail("Sprite count too large")
    sprites = bytearray(16 * 1024 * 1024) # 16MiB
    sprites[:] = b'\xff' * len(sprites)
    for _ in range(sprite_count):
        first_word = read_u32(data, offset); offset += 4
        word_count = read_u32(data, offset); offset += 4
        
        # Validate bounds before reading
        if first_word + word_count > 4194304: fail("Sprite block out of bounds")
        
        block_data = read_bytes(data, offset, word_count * 4)
        offset += word_count * 4
        pad = (4 - ((word_count * 4) % 4)) % 4
        if pad > 0:
            read_bytes(data, offset, pad)
            offset += pad
        
        start_byte = first_word * 4
        end_byte = start_byte + len(block_data)
        if end_byte > len(sprites): fail("Sprite out of bounds")
        sprites[start_byte:end_byte] = block_data

    # PCM Header
    if offset + 6144 > len(data): fail("Missing PCM header")
    pcm_header = data[offset:offset+6144]
    offset += 6144

    # PCM Blocks
    pcm_count = read_u32(data, offset); offset += 4
    if pcm_count > max_count: fail("PCM count too large")
    pcm = bytearray(4 * 1024 * 1024) # 4MiB
    pcm[:] = b'\xff' * len(pcm)
    for _ in range(pcm_count):
        p_offset = read_u32(data, offset); offset += 4
        p_length = read_u32(data, offset); offset += 4
        if p_length > 10000000: fail("PCM block too large")
        p_data = read_bytes(data, offset, p_length)
        offset += p_length
        pad = (4 - (p_length % 4)) % 4
        if pad > 0:
            read_bytes(data, offset, pad)
            offset += pad
        
        if p_offset + p_length > len(pcm): fail("PCM out of bounds")
        pcm[p_offset:p_offset+p_length] = p_data

    if offset != len(data): fail("Trailing bytes in input")

    # Apply PCM Header LAST
    pcm[0:6144] = pcm_header

    # Build core.assets
    # core_prefix preserves ALL original bytes including counts/padding
    core_assets = core_prefix + b'\x00' * 4096 + struct.pack('<I', 0) + pcm_header + struct.pack('<I', 0)

    # Create Output
    output_dir.mkdir(parents=True)
    
    def write_file(name, content):
        p = output_dir / name
        p.write_bytes(content)
        return p

    files = {
        'core.assets': core_assets,
        'tiles.bin': bytes(tiles),
        'sprites.bin': bytes(sprites),
        'pcm.bin': bytes(pcm)
    }

    metadata = {
        "input_tableversion": table_version,
        "note": "private assets extracted from user ROM; do not distribute",
        "files": {}
    }

    for name, content in files.items():
        p = write_file(name, content)
        sha256 = hashlib.sha256(content).hexdigest()
        metadata["files"][name] = {
            "size": len(content),
            "sha256": sha256
        }

    (output_dir / "metadata.json").write_text(json.dumps(metadata, indent=2))

if __name__ == '__main__':
    main()
