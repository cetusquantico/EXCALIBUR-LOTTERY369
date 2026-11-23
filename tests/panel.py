"""Exercise the actual editor and the corresponding engine plans under X11."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "panel"))
import app

exe = Path(sys.argv[1]).resolve()


def main():
    with tempfile.TemporaryDirectory() as work:
        os.environ["XDG_CONFIG_HOME"] = work
        root = app.tk.Tk()
        try:
            editor = app.Panel(root, exe, app.config_path())
            root.update()
            assert len(editor.buttons) == 256
            assert editor.first == {0x4a,0x5b,0x5e,0x67,0x6a,0x6f,0x7a}
            assert len(editor.shared) == 32 and editor.link_first.get()
            assert editor.count.get() == "7.516.192.768"
            assert not editor.path.exists()
            with patch.object(app.messagebox, "showerror", side_effect=AssertionError):
                editor.validate()
                assert "Validado" in editor.status.get()
                editor.save()
                cmd = [str(exe), "--volante", editor.name.get(), "--dry-run", "--output", "kv"]
                data = dict(line.split("=", 1) for line in subprocess.check_output(cmd, text=True).splitlines())
                assert data["total"] == "7516192768"
                assert data["ticket_id"] == "face2df889c0a3a1ba7f8c70f5541d2f1d7ab8d60bb833a8aded9e0c03e52a9e"
                editor.buttons[0x40].invoke()
                assert len(editor.shared) == 33 and 0x40 in editor.first
                editor.all_bytes()
                assert len(editor.shared) == 256 and len(editor.first) == 64
                editor.mode.set("first_allow")
                editor.switch()
                assert editor.buttons[0x40]["state"] == "disabled"
                editor.link_button.invoke()
                editor.buttons[0x40].invoke()
                assert 0x40 not in editor.first and len(editor.shared) == 256
                assert editor.buttons[0x80]["state"] == "disabled"
                editor.mode.set("shared_allow")
                editor.switch()
                shared_file = Path(work) / "compartilhada.txt"
                with patch.object(app.filedialog, "asksaveasfilename", return_value=str(shared_file)):
                    editor.export_list()
                assert len(app.parse_list(shared_file.read_text())) == 256
                shared_file.write_text("02,0A,ff")
                with patch.object(app.filedialog, "askopenfilename", return_value=str(shared_file)):
                    editor.import_list()
                assert editor.shared == {2, 10, 255}
                editor.name.set("Teste · 256")
                editor.save()
                assert set(app.load_library(editor.path)) == {app.SAMPLE_NAME, "Teste · 256"}
                editor.open_ticket(app.SAMPLE_NAME, editor.library[app.SAMPLE_NAME])
                assert len(editor.shared) == 32 and editor.link_first.get()
                editor.copy()
                assert "--first-allow 4a,5b,5e,67,6a,6f,7a" in root.clipboard_get()
                assert editor.buttons[255].winfo_height() > 12
        finally:
            root.destroy()


if __name__ == "__main__":
    main()
