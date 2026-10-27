#!/bin/sh
# Local fix for a JUCE use-after-free in XEmbedComponent on Linux (hosted LV2
# plugin GUIs are embedded through it).
#
# When the embedded client window gets a ConfigureNotify and the component does
# not let the client resize it, the Pimpl queues
#   MessageManager::callAsync ([this] { componentMovedOrResized (...); });
# with a raw `this`. A plugin GUI that keeps resizing itself queues these
# continuously; closing its window then deletes the component with callbacks
# still queued, and the next one runs on freed memory (SIGSEGV in the message
# loop). This patch makes the callback check that the owner still exists.
#
# libs/juce is fetched (gitignored), so reapply after any clean configure:
#   sh script/patch_juce_xembed.sh               # from the repo root
#
# Idempotent: safe to run repeatedly.
set -e
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
F="${1:-$REPO_DIR/libs/juce/modules/juce_gui_extra/native/juce_XEmbedComponent_linux.cpp}"

if [ ! -f "$F" ]; then
  echo "not found: $F (configure/build first so JUCE is fetched)" >&2
  exit 1
fi

if grep -q "safeOwner = Component::SafePointer" "$F"; then
  echo "already patched: $F"
  exit 0
fi

python3 - "$F" <<'PY'
import sys
path = sys.argv[1]
src = open(path).read()
anchor = "                        MessageManager::callAsync ([this] { componentMovedOrResized (owner, true, true); });\n"
fix = (
    "                        // Local fix: the component may be deleted before this runs.\n"
    "                        MessageManager::callAsync ([this, safeOwner = Component::SafePointer<Component> (&owner)]\n"
    "                        {\n"
    "                            if (safeOwner != nullptr)\n"
    "                                componentMovedOrResized (owner, true, true);\n"
    "                        });\n"
)
if anchor not in src:
    sys.exit("anchor not found — JUCE may have changed; patch manually")
open(path, "w").write(src.replace(anchor, fix, 1))
print("patched", path)
PY

