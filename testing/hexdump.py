import sys
import binascii

if len(sys.argv) is not 2:
    print('hexdump.py <inputfile>')
    sys.exit(1)

b_logged = 0
with open(sys.argv[1], 'rb') as f:
    byte = f.read(1)
    while byte != b"":
        hex_byte = binascii.hexlify(byte)
        if (b_logged % 20 == 0):
            if (b_logged > 0):
                print('\n', end='')
            print(str(b_logged).zfill(8), end='')
        print(' ' + hex_byte.decode(), end='')
        b_logged += 1
        byte = f.read(1)
print('\n', end='')