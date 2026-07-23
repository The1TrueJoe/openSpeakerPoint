# Board files live under configs/ and board/. Custom app packages (see
# apps/README.md) live under package/<name>/ and must be included here
# explicitly - BR2_EXTERNAL trees don't auto-scan package/*/*.mk the way
# the in-tree package/ directory does.
include $(sort $(wildcard $(BR2_EXTERNAL_SPEAKERPOINT_PATH)/package/*/*.mk))
