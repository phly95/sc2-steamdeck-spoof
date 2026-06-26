import struct
import sys

def parse_btsnoop(filepath):
    print(f"Parsing btsnoop file: {filepath}")
    with open(filepath, 'rb') as f:
        header = f.read(16)
        if len(header) < 16:
            return
        magic, version, datalink = struct.unpack('>8sII', header)
        
        pkt_count = 0
        while True:
            pkt_header = f.read(24)
            if len(pkt_header) < 24:
                break
            orig_len, incl_len, flags, drops, ts = struct.unpack('>IIIIQ', pkt_header)
            data = f.read(incl_len)
            pkt_count += 1
            
            is_recv = (flags & 1) != 0
            is_cmd_evt = (flags & 2) != 0
            direction = "RECV" if is_recv else "SENT"
            
            if len(data) == 0:
                continue
                
            if datalink == 2001 and not is_cmd_evt:
                payload = data
                if len(payload) >= 8:
                    handle_flags, acl_len = struct.unpack_from('<HH', payload, 0)
                    l2cap_len, cid = struct.unpack_from('<HH', payload, 4)
                    
                    if cid == 0x0004: # ATT channel
                        att_payload = payload[8:]
                        if len(att_payload) > 0:
                            opcode = att_payload[0]
                            # Only print read/write requests/responses
                            if opcode in (0x0a, 0x0b, 0x12, 0x13, 0x52):
                                op_names = {
                                    0x0a: "Read Req",
                                    0x0b: "Read Rsp",
                                    0x12: "Write Req",
                                    0x13: "Write Rsp",
                                    0x52: "Write Cmd"
                                }
                                op_name = op_names.get(opcode)
                                if opcode in (0x12, 0x52) and len(att_payload) >= 3:
                                    handle = struct.unpack_from('<H', att_payload, 1)[0]
                                    val = att_payload[3:]
                                    print(f"[{pkt_count}] {direction} ATT {op_name}: handle=0x{handle:04x} val={val.hex()}")
                                elif opcode == 0x0a and len(att_payload) >= 3:
                                    handle = struct.unpack_from('<H', att_payload, 1)[0]
                                    print(f"[{pkt_count}] {direction} ATT {op_name}: handle=0x{handle:04x}")
                                elif opcode == 0x0b:
                                    val = att_payload[1:]
                                    print(f"[{pkt_count}] {direction} ATT {op_name}: val={val.hex()}")
                                else:
                                    print(f"[{pkt_count}] {direction} ATT {op_name}: {att_payload.hex()}")
        print("Done")

if __name__ == "__main__":
    filepath = "scratch/sc2_haptic_test.log"
    if len(sys.argv) > 1:
        filepath = sys.argv[1]
    parse_btsnoop(filepath)
