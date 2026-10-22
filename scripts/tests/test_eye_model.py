import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODEL = ROOT / "main" / "boards" / "folotoy" / "ai-passport"


class EyeModelTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("c++")
        if compiler is None:
            raise unittest.SkipTest("A host C++ compiler is required")
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        cls.binary = Path(cls.directory.name) / "eye_model_test"
        subprocess.run(
            [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(MODEL),
                "-I",
                str(ROOT / "main"),
                str(MODEL / "eye_model.cc"),
                str(Path(__file__).with_name("eye_model_test.cc")),
                "-o",
                str(cls.binary),
            ],
            check=True,
        )

    def run_case(self, case):
        subprocess.run([str(self.binary), case], check=True, timeout=10)

    def test_idle_timeout_and_button_wakeup(self):
        self.run_case("idle")

    def test_active_conversation_and_setup_cannot_sleep(self):
        self.run_case("active")

    def test_stt_thinking_answer_and_next_listening_turn(self):
        self.run_case("timeline")

    def test_transient_emotion_priority_and_error_visibility(self):
        self.run_case("emotions")

    def test_millisecond_clock_rollover(self):
        self.run_case("rollover")

    def test_animated_poses_stay_within_screen_bounds(self):
        self.run_case("animation")

    def test_subtitle_scroll_dwell_speed_and_repeat(self):
        self.run_case("subtitles")


if __name__ == "__main__":
    unittest.main()
