import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import textwrap
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class WarmupSettingsRuntimeTest(unittest.TestCase):
    def test_replayed_settings_keep_session_but_changes_cancel_it(self):
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        if not compiler:
            self.skipTest("C++ compiler unavailable")
        yaml = (ROOT / "openquatt/oq_warmup.yaml").read_text()
        callbacks = []
        for name in ("oq_warmup_trigger_c", "oq_warmup_step_c", "oq_warmup_step_minutes"):
            match = re.search(
                rf"    id: {name}\n.*?    on_value:\n      - lambda: \|-\n(.*?)(?=\n\n|\Z)",
                yaml,
                re.S,
            )
            self.assertIsNotNone(match, name)
            callbacks.append(textwrap.dedent(match.group(1)))

        # Compile the actual YAML callbacks against the real runtime, including
        # template-number re-publication and pre-restore setup callbacks.
        source = r'''
#include <cassert>
#include "openquatt/includes/control/oq_warmup_runtime.h"
#define id(name) name
int main() {
  bool oq_warmup_settings_ready = true;
  float oq_warmup_settings[6] = {1, 1.5f, 0.1f, 45, 0.5f, 8};
  auto &runtime = oq_warmup_runtime::runtime();
  auto start = [&]() {
    runtime = {};
    oq_warmup::Input in{1000, true, true, true, 1, 1, 17, 17, 0.2f};
    runtime.update(in, {});
    in.requested_c = 20;
    runtime.update(in, {});
    in.now_ms += 60000;
    runtime.update(in, {});
    assert(runtime.active());
    return in;
  };
'''
        for index, callback in enumerate(callbacks, 1):
            source += f"""
  {{
    auto callback = [&](float x) {{
{callback}
    }};
    auto in = start();
    const float saved = oq_warmup_settings[{index}];
    const float target = runtime.intermediate_target();
    callback(saved);
    assert(runtime.active() && runtime.intermediate_target() == target);
    assert(runtime.session_elapsed_s(in.now_ms) == 60);
    in.now_ms += 60000;
    runtime.update(in, {{}});
    assert(runtime.active() && runtime.session_elapsed_s(in.now_ms) == 120);
    callback(saved);
    assert(runtime.active());
    callback(saved + 0.1f);
    assert(oq_warmup_settings[{index}] == saved + 0.1f);
    assert(!runtime.active() && runtime.status() == oq_warmup::Status::SETTINGS_CHANGED);
    runtime.update(in, {{}});
    assert(!runtime.active()); // Unchanged thermostat goal never restarts warmup.
    oq_warmup_settings[{index}] = saved;
    in = start();
    oq_warmup_settings_ready = false;
    callback(saved + 0.1f);
    assert(oq_warmup_settings[{index}] == saved && runtime.active());
    oq_warmup_settings_ready = true;
  }}
"""
        source += "}\n"
        with tempfile.TemporaryDirectory(prefix="openquatt-warmup-settings-") as directory:
            cpp = pathlib.Path(directory) / "callbacks.cpp"
            binary = pathlib.Path(directory) / "callbacks"
            cpp.write_text(source)
            subprocess.run(
                [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", f"-I{ROOT}", str(cpp), "-o", str(binary)],
                check=True, capture_output=True, text=True,
            )
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
