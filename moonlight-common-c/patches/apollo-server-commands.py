"""Adds Apollo's "execute server command" control message to moonlight-common-c.

Applied by the launcher's Windows build after the submodule checkout (line endings of the
checkout are kept; running it twice changes nothing). Mirrors Artemis' moonlight-common-c:
packet type 0x3000 on the encrypted Gen7 control stream, channel 0x08.
"""
import pathlib
import sys

src = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "moonlight-common-c/moonlight-common-c/src")


def patch(name, edits):
    path = src / name
    data = path.read_bytes().decode("utf-8")
    nl = "\r\n" if "\r\n" in data else "\n"
    for marker, old, new in edits:
        if marker in data:
            continue
        old_n, new_n = old.replace("\n", nl), new.replace("\n", nl)
        if data.count(old_n) < 1:
            sys.exit(f"{name}: anchor not found: {old.splitlines()[0]}")
        data = data.replace(old_n, new_n)
    path.write_bytes(data.encode("utf-8"))


patch("Limelight-internal.h", [
    ("CTRL_CHANNEL_SERVERCTL",
     "#define CTRL_CHANNEL_UTF8         0x06\n",
     "#define CTRL_CHANNEL_UTF8         0x06\n#define CTRL_CHANNEL_SERVERCTL    0x08\n"),
])

patch("Limelight.h", [
    ("LiSendExecServerCmd",
     "bool LiGetHdrMetadata(PSS_HDR_METADATA metadata);\n",
     "bool LiGetHdrMetadata(PSS_HDR_METADATA metadata);\n\n"
     "// Runs one of the host's server commands by its index (Apollo protocol extension).\n"
     "int LiSendExecServerCmd(uint8_t cmdId);\n"),
])

unused = "    -1,     // Adaptive triggers (unused)\n"
patch("ControlStream.c", [
    ("IDX_EXEC_SERVER_CMD 13",
     "#define IDX_DS_ADAPTIVE_TRIGGERS 12\n",
     "#define IDX_DS_ADAPTIVE_TRIGGERS 12\n#define IDX_EXEC_SERVER_CMD 13\n"),
    ("// Execute server command (unused)",
     unused, unused + "    -1,     // Execute server command (unused)\n"),
    ("0x3000, // Execute server command",
     "    0x5503, // Set Adaptive Triggers (Sunshine protocol extension)\n",
     "    0x5503, // Set Adaptive Triggers (Sunshine protocol extension)\n"
     "    0x3000, // Execute server command (Apollo protocol extension)\n"),
    ("int LiSendExecServerCmd(uint8_t cmdId) {",
     "bool LiGetCurrentHostDisplayHdrMode(void) {\n",
     "int LiSendExecServerCmd(uint8_t cmdId) {\n"
     "    uint8_t payload[4] = { cmdId, 0, 0, 0 };\n"
     "\n"
     "    if (packetTypes[IDX_EXEC_SERVER_CMD] == -1) {\n"
     "        return -2;\n"
     "    }\n"
     "    return sendMessageAndForget(packetTypes[IDX_EXEC_SERVER_CMD], sizeof(payload), payload,\n"
     "                                CTRL_CHANNEL_SERVERCTL, ENET_PACKET_FLAG_RELIABLE, false) ? 0 : -1;\n"
     "}\n"
     "\n"
     "bool LiGetCurrentHostDisplayHdrMode(void) {\n"),
])
print("apollo-server-commands: applied")
