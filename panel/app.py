#!/usr/bin/env python3
"""Editor local de volantes para o EXCALIBUR LOTTERY369."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import unicodedata

try:
    import tkinter as tk
    from tkinter import filedialog, font, messagebox, ttk
except ImportError:
    tk = None

SAMPLE_NAME = "Puzzle 55 · 32 bytes"
GROUP_DIR = Path(__file__).resolve().parents[1] / "grupos"
TEAL = "#086954"
DARK = "#142c29"
PAPER = "#f4f6f3"
INK = "#172e28"
SUBTLE = "#456057"
WHITE = "#ffffff"


def config_path():
    xdg = Path(os.environ.get("XDG_CONFIG_HOME", ""))
    base = xdg if xdg.is_absolute() else Path.home() / ".config"
    return base / "exlottery" / "volantes.json"


def first_bounds(puzzle):
    if type(puzzle) is not int or not 1 <= puzzle <= 160:
        raise ValueError("O puzzle deve ser um inteiro entre 1 e 160.")
    low = 1 << ((puzzle - 1) % 8)
    return low, 2 * low - 1


def format_bytes(values):
    return ",".join(f"{v:02x}" for v in sorted(values))


REFERENCE_RATE = 2_000_000   # candidatos/s de referência para a estimativa de varredura (i5-3470)


def format_coverage(fraction):
    """Fração do intervalo coberta, em porcentagem legível (notação científica p/ valores ínfimos)."""
    return f"{fraction * 100.0:.3g} %"


def format_duration(seconds):
    for name, size in (("anos", 31557600.0), ("dias", 86400.0), ("h", 3600.0), ("min", 60.0)):
        if seconds >= size:
            value = seconds / size
            return f"{value:.3g} {name}" if value >= 1000 else f"{value:.1f} {name}"
    return f"{seconds:.1f} s" if seconds >= 1 else "< 1 s"


def parse_list(text, low=0, high=255):
    if not isinstance(text, str) or not re.fullmatch(
        r"\s*[a-fA-F0-9]{2}(?:(?:\s*,\s*|\s+)[a-fA-F0-9]{2})*\s*", text
    ):
        raise ValueError("Use dois dígitos por byte, separados por vírgulas ou espaços.")
    values = [int(v, 16) for v in re.findall(r"[a-fA-F0-9]{2}", text)]
    if len(values) != len(set(values)):
        raise ValueError("Cada byte pode aparecer apenas uma vez na lista.")
    if min(values) < low or max(values) > high:
        raise ValueError(f"A lista aceita bytes de {low:02X} a {high:02X}.")
    return set(values)


def validate_name(name):
    if (not isinstance(name, str) or not 1 <= len(name) <= 80 or name != name.strip()
            or any(unicodedata.category(char).startswith("C") for char in name)):
        raise ValueError("Use de 1 a 80 caracteres no nome, sem controles ou espaços nas extremidades.")


def record(puzzle, first, shared):
    low, high = first_bounds(puzzle)
    if not first or not shared:
        raise ValueError("Selecione pelo menos um byte em cada lista.")
    if min(first) < low or max(first) > high or min(shared) < 0 or max(shared) > 255:
        raise ValueError(f"O primeiro byte do puzzle {puzzle} deve estar entre {low:02X} e {high:02X}.")
    return {"puzzle": puzzle, "first_allow": format_bytes(first), "shared_allow": format_bytes(shared)}


def ticket_args(ticket):
    return ["--puzzle", str(ticket["puzzle"]), "--first-allow", ticket["first_allow"],
            "--allow", ticket["shared_allow"]]


def load_library(path):
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return {}
    if (not isinstance(data, dict) or type(data.get("version")) is not int or data["version"] != 1
            or not isinstance(data.get("volantes"), dict)):
        raise ValueError("Formato de catálogo de volantes desconhecido.")
    for name, ticket in data["volantes"].items():
        validate_name(name)
        if (not isinstance(ticket, dict) or set(ticket) - {"puzzle", "first_allow", "shared_allow", "link_first"}
                or not {"puzzle", "first_allow", "shared_allow"} <= set(ticket)):
            raise ValueError("Registro incompleto no catálogo.")
        lo, hi = first_bounds(ticket["puzzle"])
        first = parse_list(ticket["first_allow"], lo, hi)
        shared = parse_list(ticket["shared_allow"])
        if ("link_first" in ticket and type(ticket["link_first"]) is not bool
                or ticket.get("link_first") and first != {b for b in shared if lo <= b <= hi}):
            raise ValueError("Vínculo do primeiro byte inválido no catálogo.")
        if record(ticket["puzzle"], first, shared) != {k:ticket[k] for k in ("puzzle", "first_allow", "shared_allow")}:
            raise ValueError("O catálogo contém uma lista fora da ordem canônica.")
    return data["volantes"]


def update_library(path, name, previous, ticket):
    """Merge under lock and reject stale edits to the same entry."""
    validate_name(name)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.with_suffix(".lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        library = load_library(path)
        if previous != library.get(name):
            raise ValueError("Este volante mudou em outra janela. Reabra o painel para recarregá-lo.")
        if ticket is None:
            library.pop(name)
        else:
            library[name] = ticket
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent, delete=False) as f:
            tmp = Path(f.name)
            try:
                json.dump({"version": 1, "volantes": library}, f, ensure_ascii=False,
                          sort_keys=True, indent=2)
                f.write("\n")
                f.flush()
                os.fsync(f.fileno())
                os.replace(tmp, path)
            finally:
                tmp.unlink(missing_ok=True)
    return library


class Panel:
    def __init__(self, root, binary, path):
        self.root, self.binary, self.path = root, Path(binary), Path(path)
        self.library = load_library(self.path)
        self.selected = None
        self.original = None
        self.link_first = tk.BooleanVar(value=False)
        self.active = "shared_allow"
        self.first, self.shared = set(), set()
        self.buttons = []
        self._styles()
        root.title("Volantes de bytes · EXCALIBUR LOTTERY369")
        root.configure(bg=PAPER)
        root.geometry(f"{min(1024, root.winfo_screenwidth()-24)}x{min(716, root.winfo_screenheight()-70)}")
        root.minsize(820, 620)
        root.columnconfigure(0, weight=1)
        root.rowconfigure(1, weight=1)
        top = tk.Frame(root, bg=DARK, padx=20, pady=8)
        top.grid(row=0, column=0, sticky="ew")
        tk.Label(top, text="EXCALIBUR LOTTERY369", bg=DARK, fg="#a1d4bd",
                 font=(self.sans,-11,"bold")).pack(anchor="w")
        tk.Label(top, text="Volantes de bytes", bg=DARK, fg=WHITE,
                 font=(self.sans,-24,"bold")).pack(anchor="w")
        body = tk.Frame(root, bg=PAPER, padx=12, pady=10)
        body.grid(row=1, column=0, sticky="nsew")
        body.rowconfigure(0, weight=1)
        body.columnconfigure(1, weight=1)
        side = tk.Frame(body, bg=WHITE, padx=10, pady=8, width=240)
        side.grid(row=0, column=0, sticky="ns", padx=(0,10))
        side.grid_propagate(False)
        side.columnconfigure(0, weight=1)
        self._sidebar(side)
        editor = tk.Frame(body, bg=WHITE, padx=10, pady=8)
        editor.grid(row=0, column=1, sticky="nsew")
        self._editor(editor)
        footer = tk.Frame(root, bg="#e3ece4", padx=14, pady=8)
        footer.grid(row=2, column=0, sticky="ew")
        self.status = tk.StringVar()
        tk.Label(footer, textvariable=self.status, anchor="w", justify="left",
                 wraplength=410, fg=INK, bg="#e3ece4", font=(self.sans,-12)).pack(side="left", fill="x", expand=True)
        files = ttk.Menubutton(footer, text="Lista .txt")
        menu = tk.Menu(files, tearoff=False)
        menu.add_command(label="Abrir lista da aba…", command=self.import_list)
        menu.add_command(label="Exportar lista da aba…", command=self.export_list)
        files.configure(menu=menu)
        files.pack(side="left", padx=3)
        ttk.Button(footer, text="Copiar argumentos", command=self.copy).pack(side="left", padx=6)
        ttk.Button(footer, text="Validar volante", style="Accent.TButton",
                   command=self.validate).pack(side="left")
        self._list()
        if SAMPLE_NAME in self.library:
            initial = self.library[SAMPLE_NAME]
        else:
            initial = record(55, parse_list((GROUP_DIR / "primeiro.txt").read_text()),
                             parse_list((GROUP_DIR / "compartilhada.txt").read_text()))
            low, high = first_bounds(initial["puzzle"])
            initial["link_first"] = parse_list(initial["first_allow"]) == {
                b for b in parse_list(initial["shared_allow"]) if low <= b <= high}
        self.open_ticket(SAMPLE_NAME, initial)
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def _styles(self):
        fonts = set(font.families(self.root))
        self.sans = next((x for x in ("Inter","Roboto","Adwaita Sans","DejaVu Sans") if x in fonts), "TkDefaultFont")
        self.mono = next((x for x in ("IBM Plex Mono","DejaVu Sans Mono","Courier") if x in fonts), "TkFixedFont")
        style = ttk.Style(self.root)
        style.theme_use("clam")
        style.configure(".", font=(self.sans,-13))
        style.configure("TButton", padding=(8,4), background="#edf2eb", foreground=INK)
        style.map("TButton", background=[("active","#dceade")])
        style.configure("Accent.TButton", background=TEAL, foreground=WHITE)
        style.map("Accent.TButton", background=[("active","#075340")])
        style.configure("TEntry", padding=4)
        style.configure("TCombobox", padding=3)
        style.configure("Mode.TRadiobutton", padding=3, background=WHITE, foreground=INK)

    def _label(self, parent, text, row, big=False):
        tk.Label(parent, text=text, anchor="w", bg=WHITE, fg=INK if big else SUBTLE,
                 font=(self.sans,-16 if big else -12,"bold")).grid(
                     row=row, column=0, sticky="ew", pady=(6,3))

    def _sidebar(self, side):
        self._label(side, "Volantes salvos", 0, True)
        self.saved = tk.Listbox(side, height=4, font=(self.sans,-13), exportselection=False,
                                bg="#f3f6f2", fg=INK, selectbackground=TEAL,
                                selectforeground=WHITE, highlightthickness=1, relief="flat")
        self.saved.grid(row=1, column=0, sticky="nsew")
        self.saved.bind("<<ListboxSelect>>", self.choose)
        side.rowconfigure(1, weight=1)
        actions = tk.Frame(side,bg=WHITE)
        actions.grid(row=2,column=0,sticky="ew",pady=4)
        ttk.Button(actions, text="Novo", command=self.new).pack(side="left")
        ttk.Button(actions, text="Excluir", command=self.delete).pack(side="right")
        self._label(side,"Nome do volante",3)
        self.name = tk.StringVar()
        ttk.Entry(side, textvariable=self.name).grid(row=4,column=0,sticky="ew")
        ttk.Button(side, text="Salvar", command=self.save).grid(row=5,column=0,sticky="ew",pady=(4,6))
        ttk.Separator(side).grid(row=6,column=0,sticky="ew",pady=4)
        self._label(side,"Puzzle",7)
        self.puzzle = tk.StringVar()
        puzzle = ttk.Combobox(side, textvariable=self.puzzle, values=list(range(1,161)), state="readonly")
        puzzle.grid(row=8,column=0,sticky="ew")
        puzzle.bind("<<ComboboxSelected>>", self.change_puzzle)
        self._label(side,"Candidatos",9)
        self.count = tk.StringVar()
        self._output(side,self.count,10)
        self._label(side,"Cobertura (chance real)",11)
        self.coverage = tk.StringVar()
        self._output(side,self.coverage,12)
        self._label(side,"Varredura (~2 M/s)",13)
        self.sweep = tk.StringVar()
        self._output(side,self.sweep,14)
        tk.Label(side,text="Cobertura = fração do intervalo que o volante varre, ou seja a "
                 "chance real de a chave cair nele. A chave é sorteio uniforme: nenhum byte é "
                 "mais provável que outro.",bg=WHITE,fg=SUBTLE,anchor="w",justify="left",
                 wraplength=215,font=(self.sans,-11)).grid(row=15,column=0,sticky="ew",pady=(2,4))
        self._label(side,"Primeira combinação",16)
        self.minimum = tk.StringVar()
        self._output(side,self.minimum,17)
        self._label(side,"Última combinação",18)
        self.maximum = tk.StringVar()
        self._output(side,self.maximum,19)
        self._label(side,"Bytes selecionados",20)
        self.sizes = tk.StringVar()
        self._output(side,self.sizes,21)

    def _output(self, parent, variable, row):
        tk.Label(parent,textvariable=variable,bg=WHITE,fg=INK,anchor="w",justify="left",
                 wraplength=215,font=(self.mono,-12)).grid(row=row,column=0,sticky="ew")

    def _editor(self, editor):
        editor.columnconfigure(0,weight=1)
        editor.rowconfigure(2,weight=1)
        tabs = tk.Frame(editor,bg=WHITE)
        tabs.grid(row=0,column=0,sticky="ew")
        self.mode = tk.StringVar(value=self.active)
        for text, value in [("Primeiro byte","first_allow"),("Bytes compartilhados","shared_allow")]:
            ttk.Radiobutton(tabs,text=text,value=value,variable=self.mode,style="Mode.TRadiobutton",
                            command=self.switch).pack(side="left",padx=(0,5))
        ttk.Button(tabs,text="Todos",width=7,command=self.all_bytes).pack(side="right")
        ttk.Button(tabs,text="Limpar",width=7,command=self.clear).pack(side="right",padx=4)
        line = tk.Frame(editor, bg=WHITE)
        line.grid(row=1,column=0,sticky="ew",pady=(5,3))
        self.link_button = ttk.Checkbutton(line, text="Vincular primeiro byte à seleção",
                                            variable=self.link_first, command=self.change_link)
        self.link_button.pack(side="right")
        self.info = tk.StringVar()
        tk.Label(line,textvariable=self.info,bg=WHITE,fg=SUBTLE,anchor="w",
                 font=(self.sans,-12)).pack(side="left")
        board = tk.Frame(editor,bg=WHITE)
        board.grid(row=2,column=0,sticky="nsew")
        for i in range(16):
            board.rowconfigure(i,weight=1,uniform="rows")
            board.columnconfigure(i,weight=1,uniform="cols")
        for value in range(256):
            b = tk.Button(board,text=f"{value:02X}",font=(self.mono,-12),
                          width=2,padx=0,pady=0,borderwidth=1,highlightthickness=1,
                          highlightbackground=WHITE,highlightcolor="#b76e00",
                          command=lambda n=value:self.toggle(n))
            b.grid(row=value//16,column=value%16,sticky="nsew",padx=1,pady=1)
            for key, step in [("Left",-1),("Right",1),("Up",-16),("Down",16)]:
                b.bind("<"+key+">", lambda e,n=value,d=step:self.move_focus(n,d))
            self.buttons.append(b)
        self._label(editor, "Lista hexadecimal",3)
        row = tk.Frame(editor,bg=WHITE)
        row.grid(row=4,column=0,sticky="ew")
        self.entry = tk.StringVar()
        self.input = ttk.Entry(row,textvariable=self.entry,font=(self.mono,-13))
        self.input.pack(side="left",fill="x",expand=True)
        self.input.bind("<Return>", lambda e:self.apply())
        ttk.Button(row,text="Aplicar lista",command=self.apply).pack(side="right",padx=(6,0))

    def values(self):
        return self.first if self.active=="first_allow" else self.shared

    def current(self):
        ticket = record(int(self.puzzle.get()), self.first, self.shared)
        ticket["link_first"] = self.link_first.get()
        return ticket

    def bounds(self):
        return first_bounds(int(self.puzzle.get())) if self.active=="first_allow" else (0,255)

    def _list(self):
        self.saved.delete(0,"end")
        for name in sorted(self.library, key=str.casefold):
            self.saved.insert("end",name)

    def _select_name(self):
        self.saved.selection_clear(0,"end")
        names = self.saved.get(0,"end")
        if self.selected in names:
            index = names.index(self.selected)
            self.saved.selection_set(index)
            self.saved.see(index)

    def open_ticket(self, name, ticket):
        self.selected = name if name in self.library else None
        self.name.set(name)
        self.puzzle.set(str(ticket["puzzle"]))
        self.first = parse_list(ticket["first_allow"])
        self.shared = parse_list(ticket["shared_allow"])
        self.link_first.set(ticket.get("link_first", False))
        self.original = (name, int(self.puzzle.get()), self.first.copy(), self.shared.copy(), self.link_first.get())
        self._select_name()
        self.render()

    def render(self):
        self.displayed_puzzle = int(self.puzzle.get())
        self.low,self.high = self.bounds()
        if self.link_first.get():
            low, high = first_bounds(self.displayed_puzzle)
            self.first = {byte for byte in self.shared if low <= byte <= high}
        automatic = self.active == "first_allow" and self.link_first.get()
        self.input.configure(state="readonly" if automatic else "normal")
        values = self.values()
        for value,b in enumerate(self.buttons):
            legal = self.low <= value <= self.high and not automatic
            chosen = value in values
            bg = TEAL if chosen else ("#eaf1eb" if legal else "#f8f9f7")
            b.configure(background=bg,foreground=WHITE if chosen else INK,
                        activebackground="#b3d9c6", activeforeground=INK,
                        relief="sunken" if chosen else "flat",
                        disabledforeground=WHITE if chosen else "#9baba2",
                        state="normal" if legal else "disabled",takefocus=legal)
        self.info.set(f"Faixa permitida  {self.low:02X}–{self.high:02X}     •     {len(values)} selecionados")
        self.entry.set(format_bytes(values).upper())
        self.sizes.set(f"{len(self.first)} inicial  /  {len(self.shared)} compartilhados")
        length = (int(self.puzzle.get())+7)//8
        total = len(self.first)*len(self.shared)**(length-1) if self.first and self.shared else 0
        self.count.set(f"{total:,}".replace(",","."))
        interval = 1 << (self.displayed_puzzle - 1)   # largura de [2^(N-1), 2^N)
        self.coverage.set(format_coverage(total / interval) if total else "—")
        self.sweep.set(format_duration(total / REFERENCE_RATE) if total else "—")
        for op,var in [(min,self.minimum),(max,self.maximum)]:
            var.set(" ".join([f"{op(self.first):02X}"]+[f"{op(self.shared):02X}"]*(length-1))
                    if total else "—")
        self.status.set("")

    def change_link(self):
        requested = self.link_first.get()
        self.link_first.set(not requested)
        if self.sync():
            self.link_first.set(requested)
            self.render()

    def manual(self):
        if self.active == "first_allow" and self.link_first.get():
            self.status.set("Desmarque o vínculo para editar o primeiro byte manualmente.")
            return False
        return True

    def apply(self):
        if not self.manual():
            return False
        try:
            values = parse_list(self.entry.get(), *self.bounds())
        except ValueError as exc:
            messagebox.showerror("Lista hexadecimal",str(exc),parent=self.root)
            return False
        self.values().clear()
        self.values().update(values)
        self.render()
        return True

    def pending_input(self):
        return self.entry.get() != format_bytes(self.values()).upper()

    def sync(self):
        return not self.pending_input() or self.apply()

    def move_focus(self, n, step):
        target = min(self.high,max(self.low,n+step))
        self.buttons[target].focus_set()
        return "break"

    def toggle(self,n):
        if self.manual() and self.sync() and self.low <= n <= self.high:
            self.values().symmetric_difference_update({n})
            self.render()

    def switch(self):
        if not self.sync():
            self.mode.set(self.active)
            return
        self.active = self.mode.get()
        self.render()

    def all_bytes(self):
        if not self.manual(): return
        self.values().update(range(self.low,self.high+1))
        self.render()

    def clear(self):
        if not self.manual(): return
        self.values().clear()
        self.render()

    def change_puzzle(self,event=None):
        # Keep the edited bytes visible when switching to a more restrictive range.
        # Clear only the first list, after asking, when it would contain invalid bytes.
        n = int(self.puzzle.get())
        old = self.displayed_puzzle
        self.puzzle.set(str(old))
        if not self.sync():
            return
        low,high = first_bounds(n)
        if not self.link_first.get() and any(not low <= b <= high for b in self.first):
            if not messagebox.askyesno("Alterar puzzle",
                    f"O puzzle {n} aceita o primeiro byte de {low:02X} a {high:02X}. "
                    "Limpar a lista inicial para escolher outros valores?",parent=self.root):
                return
            self.first.clear()
            self.active = "first_allow"
            self.mode.set(self.active)
        self.puzzle.set(str(n))
        self.render()

    def discard(self):
        state = (self.name.get(),int(self.puzzle.get()),self.first,self.shared,self.link_first.get())
        return (self.original == state and not self.pending_input()) or messagebox.askyesno(
            "Alterações no volante","Descartar as alterações deste volante?",parent=self.root)

    def choose(self,event=None):
        selection = self.saved.curselection()
        if not selection:
            return
        name = self.saved.get(selection[0])
        if name == self.selected:
            return
        if self.discard():
            self.open_ticket(name,self.library[name])
        else:
            self._select_name()

    def new(self):
        if self.discard():
            n = int(self.puzzle.get())
            self.open_ticket("",record(n,{first_bounds(n)[0]},{0}))

    def write(self,name,ticket):
        try:
            self.library = update_library(self.path,name,self.library.get(name),ticket)
        except (ValueError,OSError) as exc:
            messagebox.showerror("Catálogo de volantes",str(exc),parent=self.root)
            return False
        self._list()
        return True

    def save(self):
        if not self.sync():
            return
        name = self.name.get()
        try:
            validate_name(name)
            ticket = self.current()
        except ValueError as exc:
            messagebox.showerror("Salvar volante",str(exc),parent=self.root)
            return
        if name in self.library and name != self.selected:
            if not messagebox.askyesno("Salvar volante",f"Substituir o volante «{name}»?",parent=self.root):
                return
        if self.write(name,ticket):
            self.open_ticket(name,ticket)
            self.status.set(f"Volante salvo · {name}")

    def delete(self):
        name = self.selected
        if name is None:
            self.status.set("Selecione um volante salvo para excluí-lo.")
            return
        if messagebox.askyesno("Excluir volante",f"Excluir «{name}» do catálogo?",parent=self.root):
            if self.write(name,None):
                self.selected = None
                self.status.set("Volante excluído. A seleção continua no editor.")

    def copy(self):
        if not self.sync():
            return
        try:
            args = shlex.join(ticket_args(self.current()))
        except ValueError as exc:
            messagebox.showerror("Copiar argumentos",str(exc),parent=self.root)
            return
        self.root.clipboard_clear()
        self.root.clipboard_append(args)
        self.status.set("Argumentos do volante copiados para o terminal.")

    def import_list(self):
        if not self.manual(): return
        path = filedialog.askopenfilename(parent=self.root, title="Abrir lista da aba",
                                          filetypes=[("Listas de bytes", "*.txt"), ("Todos", "*")])
        if not path:
            return
        try:
            data = parse_list(Path(path).read_text(encoding="utf-8"), *self.bounds())
        except (ValueError, OSError) as exc:
            return messagebox.showerror("Abrir lista", str(exc), parent=self.root)
        self.values().clear()
        self.values().update(data)
        self.render()
        self.status.set(f"Lista carregada · {len(data)} valores")

    def export_list(self):
        if not self.sync():
            return
        if not self.values():
            return messagebox.showerror("Exportar lista", "Selecione pelo menos um byte.", parent=self.root)
        filename = "primeiro.txt" if self.active == "first_allow" else "compartilhada.txt"
        path = filedialog.asksaveasfilename(parent=self.root, title="Exportar lista da aba",
                                            initialfile=filename, defaultextension=".txt")
        if not path:
            return
        try:
            with tempfile.NamedTemporaryFile("w", dir=Path(path).parent, delete=False) as f:
                tmp = Path(f.name)
                try:
                    f.write(format_bytes(self.values()) + "\n")
                    f.flush()
                    os.fsync(f.fileno())
                    os.replace(tmp, path)
                finally:
                    tmp.unlink(missing_ok=True)
        except OSError as exc:
            return messagebox.showerror("Exportar lista", str(exc), parent=self.root)
        self.status.set(f"Lista exportada · {Path(path).name}")

    def validate(self):
        if not self.sync():
            return
        try:
            ticket = self.current()
            p = subprocess.run([str(self.binary),*ticket_args(ticket),"--dry-run","--output","kv"],
                               text=True,capture_output=True,timeout=5,check=False)
            if p.returncode:
                raise ValueError(p.stderr.strip() or "A validação pelo executável falhou.")
            data = dict(line.split("=",1) for line in p.stdout.splitlines() if "=" in line)
            if int(data["total"]) != int(self.count.get().replace(".","")):
                raise ValueError("O total calculado pelo executável diverge da prévia.")
            self.status.set(f"Validado pelo motor · {self.count.get()} candidatos")
        except (ValueError,OSError,KeyError,subprocess.TimeoutExpired) as exc:
            messagebox.showerror("Validar volante",str(exc),parent=self.root)

    def close(self):
        if self.discard():
            self.root.destroy()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, help="Caminho do executável exlottery")
    args = parser.parse_args()
    if tk is None:
        sys.exit("O painel requer Tk para Python 3. No Kali, instale python3-tk.")
    root_dir = Path(__file__).resolve().parents[1]
    binary = args.binary or next((p for p in (root_dir/"exlottery",root_dir/"build/exlottery",root_dir/"bin/exlottery")
                                  if p.is_file()),root_dir/"build/exlottery")
    try:
        root = tk.Tk()
        Panel(root,binary.resolve(),config_path())
    except (ValueError,OSError,tk.TclError) as exc:
        sys.exit(f"Não foi possível abrir o painel de volantes. {exc}")
    root.mainloop()


if __name__ == "__main__":
    main()
