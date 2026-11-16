package com.luac.desktop;

import java.awt.*;
import java.awt.event.*;
import java.io.IOException;
import java.nio.file.*;
import java.util.Locale;
import java.util.prefs.Preferences;
import javax.swing.*;
import javax.swing.border.*;
import javax.swing.event.DocumentEvent;
import javax.swing.event.DocumentListener;
import javax.swing.text.DefaultCaret;

/** Swing desktop frontend. All Lua execution remains native, through LuaC. */
public final class Studio extends JFrame {
    private static final Color BG = Material.BACKGROUND, PANEL = Material.SURFACE, TEXT = Color.WHITE, MUTED = Color.WHITE;
    private final Preferences prefs = Preferences.userNodeForPackage(Studio.class);
    final ProjectStore store;
    final Compiler compiler;
    Path project, directory, editorFile;
    final JTextArea editor = new JTextArea(), output = new JTextArea();
    final JPanel body = new JPanel(new BorderLayout()), bar = new JPanel(new BorderLayout(8, 0));
    final JLabel title = new JLabel(), subtitle = new JLabel();
    boolean dirty, loading, running, russian;
    Color accent;
    JButton runButton, stopButton;
    PaintCanvas painting;

    Studio(Path root, Path home) throws IOException {
        super("LuaC Studio");
        java.net.URL logo = Studio.class.getResource("logo.png");
        if (logo != null) setIconImage(new ImageIcon(logo).getImage());
        store = new ProjectStore(root); compiler = new Compiler(home);
        russian = prefs.getBoolean("russian", Locale.getDefault().getLanguage().equals("ru"));
        accent = new Color(prefs.getInt("accent", 0xff4bdcb5));
        setDefaultCloseOperation(DO_NOTHING_ON_CLOSE);
        addWindowListener(new WindowAdapter() { @Override public void windowClosing(WindowEvent e) { if (confirmSave() && confirmPaint()) { compiler.stop(); dispose(); } } });
        setMinimumSize(new Dimension(720, 500)); setSize(1050, 760); setLocationRelativeTo(null);
        getContentPane().setBackground(BG); getContentPane().setLayout(new BorderLayout());
        bar.setBackground(Material.dark(accent)); bar.setBorder(new EmptyBorder(8, 12, 8, 12));
        title.setForeground(TEXT); title.setFont(title.getFont().deriveFont(Font.BOLD, 20f));
        subtitle.setForeground(TEXT); subtitle.setFont(subtitle.getFont().deriveFont(12f));
        add(bar, BorderLayout.NORTH); body.setBackground(BG); add(body, BorderLayout.CENTER);
        editor.setFont(new Font(Font.MONOSPACED, Font.PLAIN, 15)); editor.setBackground(PANEL); editor.setForeground(TEXT); editor.setCaretColor(TEXT);
        editor.setBorder(new EmptyBorder(14, 14, 14, 14)); editor.setTabSize(4);
        editor.getDocument().addDocumentListener(new DocumentListener() {
            private void changed() { if (!loading) { dirty = true; updateEditorTitle(); } }
            public void insertUpdate(DocumentEvent e) { changed(); } public void removeUpdate(DocumentEvent e) { changed(); } public void changedUpdate(DocumentEvent e) { changed(); }
        });
        output.setEditable(false); output.setLineWrap(true); output.setWrapStyleWord(true); output.setRows(6);
        output.setFont(new Font(Font.MONOSPACED, Font.PLAIN, 13)); output.setBackground(PANEL); output.setForeground(TEXT); output.setBorder(new EmptyBorder(8, 12, 8, 12));
        ((DefaultCaret)output.getCaret()).setUpdatePolicy(DefaultCaret.ALWAYS_UPDATE);
        setupShortcuts(); showProjects();
    }
    private String tr(String en, String ru) { return russian ? ru : en; }
    private JButton button(String label, Runnable action) {
        JButton b = new Material.Button(label, Material.dark(accent), false); b.addActionListener(e -> action.run()); return b;
    }
    private JButton iconButton(String label, String hint, Runnable action) {
        JButton b = new Material.Button(label, null, true); b.setToolTipText(hint); b.getAccessibleContext().setAccessibleName(hint); b.addActionListener(e -> action.run()); return b;
    }
    private JPanel buttons() { JPanel p = new JPanel(new FlowLayout(FlowLayout.RIGHT, 2, 0)); p.setOpaque(false); return p; }
    private void header(String name, String detail, JPanel actions, Runnable back) {
        title.setText(name); subtitle.setText(detail == null ? "" : detail); subtitle.setVisible(detail != null && !detail.isEmpty());
        JPanel labels = new JPanel(); labels.setOpaque(false); labels.setLayout(new BoxLayout(labels, BoxLayout.Y_AXIS)); labels.add(title); labels.add(subtitle);
        bar.removeAll(); bar.setBackground(Material.dark(accent));
        if (back != null) bar.add(iconButton("←", tr("Back", "Назад"), back), BorderLayout.WEST);
        bar.add(labels, BorderLayout.CENTER); bar.add(actions, BorderLayout.EAST); bar.revalidate(); bar.repaint();
    }
    private void popup(JButton anchor, String[] names, Runnable[] actions) {
        JPopupMenu menu = new JPopupMenu();
        for (int i = 0; i < names.length; ++i) { JMenuItem item = new JMenuItem(names[i]); final Runnable action = actions[i]; item.addActionListener(e -> action.run()); menu.add(item); }
        menu.show(anchor, 0, anchor.getHeight());
    }
    private void content(JComponent component) { body.removeAll(); body.add(component, BorderLayout.CENTER); body.revalidate(); body.repaint(); }
    private void error(Exception ex) { JOptionPane.showMessageDialog(this, ex.getMessage(), tr("Error", "Ошибка"), JOptionPane.ERROR_MESSAGE); }
    private String ask(String prompt, String initial) {
        return (String)JOptionPane.showInputDialog(this, prompt, "LuaC Studio", JOptionPane.QUESTION_MESSAGE, null, null, initial);
    }
    private JPanel listPanel() {
        JPanel p = new JPanel(); p.setLayout(new BoxLayout(p, BoxLayout.Y_AXIS)); p.setBackground(BG); p.setBorder(new EmptyBorder(16, 16, 16, 16)); return p;
    }
    private JScrollPane scroll(JComponent c) {
        JScrollPane s = new JScrollPane(c); s.setBorder(null); s.getViewport().setBackground(BG); s.getVerticalScrollBar().setUnitIncrement(24); return s;
    }
    private JPanel row(String name, String detail, String type, Runnable open, JButton... actions) {
        JPanel p = new Material.Card(new BorderLayout(12, 0), type.equals("project") ? 20 : 16);
        p.setAlignmentX(Component.LEFT_ALIGNMENT); p.setBorder(new EmptyBorder(12, 14, 12, 14)); p.setMaximumSize(new Dimension(Integer.MAX_VALUE, 80));
        JPanel labels = new JPanel(); labels.setLayout(new BoxLayout(labels, BoxLayout.Y_AXIS)); labels.setOpaque(false);
        JLabel main = new JLabel(name), sub = new JLabel(detail); main.setForeground(TEXT); main.setFont(main.getFont().deriveFont(type.equals("project") ? Font.BOLD : Font.PLAIN, type.equals("project") ? 18f : 16f));
        sub.setForeground(MUTED); sub.setFont(sub.getFont().deriveFont(12f)); labels.add(main); labels.add(Box.createVerticalStrut(4)); labels.add(sub);
        p.setCursor(Cursor.getPredefinedCursor(Cursor.HAND_CURSOR));
        MouseAdapter listener = new MouseAdapter() { @Override public void mouseClicked(MouseEvent e) { open.run(); } };
        p.addMouseListener(listener); labels.addMouseListener(listener); main.addMouseListener(listener); sub.addMouseListener(listener);
        if (!type.equals("project")) { JLabel icon = new JLabel(new Material.FileIcon(type)); icon.addMouseListener(listener); p.add(icon, BorderLayout.WEST); }
        p.add(labels, BorderLayout.CENTER); JPanel group = buttons(); for (JButton b : actions) group.add(b); p.add(group, BorderLayout.EAST); return p;
    }
    private int fileCount(Path path) {
        try (java.util.stream.Stream<Path> files = Files.walk(path)) { return (int) files.filter(p -> Files.isRegularFile(p, LinkOption.NOFOLLOW_LINKS)).count(); }
        catch (IOException e) { return 0; }
    }
    private void newProject() {
        String name = ask(tr("Project name", "Название проекта"), ""); if (name == null) return;
        try { showFiles(store.createProject(name)); } catch (IOException e) { error(e); }
    }
    void showProjects() {
        if (editorFile != null && !confirmSave()) return;
        editorFile = null; project = null; directory = null; runButton = stopButton = null;
        JPanel actions = buttons();
        actions.add(iconButton("⇧", tr("Import .luac", "Импорт .luac"), this::importProject));
        actions.add(iconButton("▦", tr("Packages", "Пакеты"), this::showSamples));
        actions.add(iconButton("⚙", tr("Settings", "Настройки"), this::settings));
        header("LuaC Studio", "LuaC 0.5.0 • native compiler", actions, null);
        JPanel list = listPanel(); list.setName("projects-list");
        try {
            for (Path p : store.projects()) {
                int count = fileCount(p);
                JButton run = button("▶  " + tr("Run", "Запуск"), () -> { showFiles(p); runProject(); });
                JButton more = itemMenu(p, true);
                list.add(row(p.getFileName().toString(), russian ? "Файлов: " + count + " • проект Lua" : count + " files • Lua project", "project", () -> showFiles(p), run, more));
                list.add(Box.createVerticalStrut(12));
            }
        } catch (IOException e) { error(e); }
        JPanel page = new JPanel(new BorderLayout()); page.setBackground(BG); page.add(scroll(list), BorderLayout.CENTER);
        JPanel fab = buttons(); fab.setBorder(new EmptyBorder(0, 0, 16, 16));
        JButton newProject = button("+  " + tr("New project", "Новый проект"), this::newProject); newProject.setName("new-project"); fab.add(newProject); page.add(fab, BorderLayout.SOUTH);
        content(page);
    }
    void showFiles(Path path) {
        if (editorFile != null && !confirmSave()) return;
        editorFile = null;
        if (project == null || !path.startsWith(project)) project = path;
        directory = path; runButton = stopButton = null;
        JPanel actions = buttons();
        runButton = iconButton("▶", tr("Run", "Запуск"), this::runProject); runButton.setEnabled(!running); actions.add(runButton);
        actions.add(iconButton("+", tr("New file", "Новый файл"), () -> create(false)));
        JButton more = iconButton("⋮", tr("More options", "Другие действия"), () -> {});
        for (ActionListener listener : more.getActionListeners()) more.removeActionListener(listener);
        more.addActionListener(e -> popup(more, new String[]{tr("New folder", "Новая папка"), tr("Import asset", "Импорт файла"), tr("Paint — new image", "Paint — новый рисунок"), tr("Export project (.luac)", "Экспорт проекта (.luac)")}, new Runnable[]{() -> create(true), this::importFile, this::showPaint, () -> exportProject(project)})); actions.add(more);
        Runnable back = () -> { if (directory.equals(project)) showProjects(); else showFiles(directory.getParent()); };
        header(project.getFileName().toString(), directory.equals(project) ? tr("Project files", "Файлы проекта") : project.relativize(directory).toString(), actions, back);
        JPanel list = listPanel(); list.setName("files-list");
        try {
            for (Path p : store.children(directory)) {
                boolean folder = Files.isDirectory(p, LinkOption.NOFOLLOW_LINKS);
                String filename = p.getFileName().toString().toLowerCase(Locale.ROOT);
                boolean image = filename.endsWith(".png") || filename.endsWith(".jpg") || filename.endsWith(".jpeg");
                String type = folder ? "folder" : image ? "image" : filename.endsWith(".lua") ? "code" : "file";
                String detail = folder ? tr("Folder", "Папка") : image ? tr("Image asset", "Изображение") : filename.equals("main.lua") ? tr("Entry point", "Точка входа") : filename.endsWith(".lua") ? tr("Lua script", "Скрипт Lua") : tr("File", "Файл");
                Runnable open = () -> { if (folder) showFiles(p); else openFile(p); };
                list.add(row(p.getFileName().toString(), detail, type, open, itemMenu(p, false))); list.add(Box.createVerticalStrut(8));
            }
        } catch (IOException e) { error(e); }
        content(scroll(list));
    }
    private JButton itemMenu(Path path, boolean rootProject) {
        JButton more = iconButton("⋮", tr("More options", "Другие действия"), () -> {});
        for (ActionListener listener : more.getActionListeners()) more.removeActionListener(listener);
        more.addActionListener(e -> {
            if (rootProject) popup(more, new String[]{tr("Export .luac", "Экспорт .luac"), tr("Rename", "Переименовать"), tr("Delete", "Удалить")},
                new Runnable[]{() -> exportProject(path), () -> renameItem(path, true), () -> deleteItem(path, true)});
            else popup(more, new String[]{tr("Rename", "Переименовать"), tr("Delete", "Удалить")},
                new Runnable[]{() -> renameItem(path, false), () -> deleteItem(path, false)});
        });
        return more;
    }
    private void showSamples() {
        JPanel actions = buttons(); header(tr("Packages", "Пакеты"), tr("Examples • create an editable copy", "Примеры • создайте копию для редактирования"), actions, this::showProjects);
        JPanel list = listPanel();
        String[][] samples = {
            {"hello", "Hello, modules", "greeting.lua", tr("A main file and a project module", "Главный файл и модуль проекта")},
            {"library", "Built-in libraries", "data.lua", "JSON, SHA-256 and Base64"},
            {"gui", "luac_gui • touch", "luac_gui.lua", tr("Android Canvas example; not a desktop renderer", "Пример Android Canvas; не работает как графика на ПК")},
            {"love", "LÖVE-style drawing", "scene.lua", tr("Android Canvas example; not a desktop renderer", "Пример Android Canvas; не работает как графика на ПК")},
            {"ffi", "Native FFI", "native.lua", tr("Requires a compatible libffi", "Требуется совместимая libffi")}
        };
        for (String[] sample : samples) {
            Runnable install = () -> { try { showFiles(installSample(sample)); } catch (IOException e) { error(e); } };
            list.add(row(sample[1], sample[3], "project", install, button(tr("Create", "Создать"), install)));
            list.add(Box.createVerticalStrut(12));
        }
        content(scroll(list));
    }
    private Path installSample(String[] sample) throws IOException {
        ProjectArchive.Document doc = new ProjectArchive.Document(sample[1]);
        doc.directories.add("scripts"); doc.directories.add("assets");
        String[] paths = sample[0].equals("love") ? new String[]{"main.lua", "scripts/" + sample[2], "assets/luac_logo.png"} : new String[]{"main.lua", "scripts/" + sample[2]};
        for (String path : paths) {
            try (java.io.InputStream in = Studio.class.getResourceAsStream("/samples/" + sample[0] + "/" + path)) {
                if (in == null) throw new IOException("Sample asset missing: " + path);
                byte[] data = in.readNBytes(1024 * 1024 + 1); if (data.length > 1024 * 1024) throw new IOException("Sample too large");
                doc.files.add(new ProjectArchive.Entry(path, data));
            }
        }
        return store.installDocument(doc);
    }
    private void importProject() {
        JFileChooser chooser = new JFileChooser(); chooser.setFileFilter(new javax.swing.filechooser.FileNameExtensionFilter("LuaC project (*.luac)", "luac"));
        if (chooser.showOpenDialog(this) != JFileChooser.APPROVE_OPTION) return;
        try { showFiles(store.importProject(chooser.getSelectedFile().toPath())); } catch (IOException e) { error(e); }
    }
    private void exportProject(Path path) {
        JFileChooser chooser = new JFileChooser(); chooser.setSelectedFile(new java.io.File(path.getFileName().toString() + ".luac"));
        chooser.setFileFilter(new javax.swing.filechooser.FileNameExtensionFilter("LuaC project (*.luac)", "luac"));
        if (chooser.showSaveDialog(this) != JFileChooser.APPROVE_OPTION) return;
        Path target = chooser.getSelectedFile().toPath();
        if (!target.getFileName().toString().toLowerCase(Locale.ROOT).endsWith(".luac")) target = target.resolveSibling(target.getFileName() + ".luac");
        if (Files.exists(target) && JOptionPane.showConfirmDialog(this, tr("Replace existing file?", "Заменить существующий файл?"), "LuaC Studio", JOptionPane.YES_NO_OPTION) != JOptionPane.YES_OPTION) return;
        try { store.exportProject(path, target); } catch (IOException e) { error(e); }
    }
    private void create(boolean folder) {
        String name = ask(folder ? tr("Folder name", "Название папки") : tr("File name", "Название файла"), ""); if (name == null) return;
        try { if (folder) { store.createFolder(directory, name); showFiles(directory); } else openFile(store.createFile(directory, name)); }
        catch (IOException e) { error(e); }
    }
    private void importFile() {
        JFileChooser chooser = new JFileChooser(); if (chooser.showOpenDialog(this) != JFileChooser.APPROVE_OPTION) return;
        try {
            store.importFile(directory, chooser.getSelectedFile().toPath()); showFiles(directory);
        } catch (IOException e) { error(e); }
    }
    private void renameItem(Path path, boolean rootProject) {
        String name = ask(tr("New name (references are not updated)", "Новое имя (пути в коде не изменятся)"), path.getFileName().toString());
        if (name == null) return;
        try { store.rename(path, name); if (rootProject) showProjects(); else showFiles(directory); } catch (IOException e) { error(e); }
    }
    private void deleteItem(Path path, boolean rootProject) {
        if (JOptionPane.showConfirmDialog(this, tr("Delete permanently: ", "Удалить безвозвратно: ") + path.getFileName() + "?", "LuaC Studio", JOptionPane.YES_NO_OPTION) != JOptionPane.YES_OPTION) return;
        try { store.delete(path); if (rootProject) showProjects(); else showFiles(directory); } catch (IOException e) { error(e); }
    }
    void openFile(Path file) {
        String name = file.getFileName().toString().toLowerCase(Locale.ROOT);
        if (name.endsWith(".png") || name.endsWith(".jpg") || name.endsWith(".jpeg")) { showImage(file); return; }
        if (!(name.endsWith(".lua") || name.endsWith(".txt"))) {
            JOptionPane.showMessageDialog(this, tr("Only Lua and text files are editable here.", "Здесь можно редактировать только Lua и текстовые файлы.")); return;
        }
        try {
            String text = store.read(file); loading = true; editor.setText(text); editor.setCaretPosition(0); loading = false; dirty = false; editorFile = file;
            showEditor();
        } catch (IOException e) { loading = false; error(e); }
    }
    private boolean confirmPaint() {
        return painting == null || !painting.dirty || JOptionPane.showConfirmDialog(this, tr("Discard unsaved drawing?", "Отменить несохранённый рисунок?"), "Paint", JOptionPane.YES_NO_OPTION) == JOptionPane.YES_OPTION;
    }
    private void leavePaint() { if (confirmPaint()) { painting = null; showFiles(directory); } }
    private void showPaint() {
        painting = new PaintCanvas();
        header("Paint", tr("512 × 512 • transparent PNG", "512 × 512 • прозрачный PNG"), buttons(), this::leavePaint);
        JPanel page = new JPanel(new BorderLayout(0, 8)); page.setBackground(BG); page.setBorder(new EmptyBorder(16, 16, 16, 16)); page.add(painting, BorderLayout.CENTER);
        JPanel controls = new JPanel(); controls.setBackground(BG); controls.setLayout(new BoxLayout(controls, BoxLayout.Y_AXIS));
        JPanel row = buttons();
        row.add(button(tr("Color", "Цвет"), () -> { Color c = JColorChooser.showDialog(this, tr("Brush color", "Цвет кисти"), painting.color); if (c != null) { painting.color = c; painting.eraser = false; } }));
        row.add(button(tr("Eraser", "Ластик"), () -> painting.eraser = true)); row.add(button(tr("Undo", "Отменить"), () -> painting.undo()));
        row.add(button(tr("Clear", "Очистить"), () -> { if (JOptionPane.showConfirmDialog(this, tr("Clear drawing?", "Очистить рисунок?"), "Paint", JOptionPane.YES_NO_OPTION) == JOptionPane.YES_OPTION) painting.clear(); }));
        row.add(button(tr("Save PNG", "Сохранить PNG"), () -> {
            String name = ask(tr("Save image to assets", "Сохранить рисунок в assets"), "drawing.png"); if (name == null) return;
            try { store.savePainting(project, name, painting.image); painting.dirty = false; JOptionPane.showMessageDialog(this, tr("Saved in assets", "Сохранено в assets")); } catch (IOException e) { error(e); }
        }));
        controls.add(row);
        JSlider size = new JSlider(1, 64, 8); size.setOpaque(false); size.addChangeListener(e -> painting.size = size.getValue()); controls.add(size);
        page.add(controls, BorderLayout.SOUTH); content(page);
    }
    private void showImage(Path file) {
        try {
            // ImageIO readers allow rejecting unusually large images before decoding.
            try (javax.imageio.stream.ImageInputStream input = javax.imageio.ImageIO.createImageInputStream(file.toFile())) {
                if (input == null) throw new IOException("Cannot open image");
                java.util.Iterator<javax.imageio.ImageReader> readers = javax.imageio.ImageIO.getImageReaders(input);
                if (!readers.hasNext()) throw new IOException("Unsupported image");
                javax.imageio.ImageReader reader = readers.next();
                java.awt.image.BufferedImage image;
                try { reader.setInput(input); int w = reader.getWidth(0), h = reader.getHeight(0); if (w <= 0 || h <= 0 || (long)w * h > 32_000_000L) throw new IOException("Image is too large"); image = reader.read(0); }
                finally { reader.dispose(); }
                header(file.getFileName().toString(), tr("Image", "Изображение"), buttons(), () -> showFiles(directory));
                JLabel picture = new JLabel(new ImageIcon(image), SwingConstants.CENTER); picture.setBorder(new EmptyBorder(16, 16, 16, 16)); content(scroll(picture));
            }
        } catch (IOException e) { error(e); }
    }
    private void updateEditorTitle() { if (editorFile != null) title.setText(editorFile.getFileName().toString() + (dirty ? " *" : "")); }
    private JPanel console() {
        JPanel panel = new Material.Card(new BorderLayout(), 16); panel.setBorder(new EmptyBorder(8, 16, 8, 16));
        JLabel label = new JLabel(tr("CONSOLE", "КОНСОЛЬ")); label.setForeground(MUTED); panel.add(label, BorderLayout.NORTH); panel.add(scroll(output), BorderLayout.CENTER); panel.setPreferredSize(new Dimension(300, 145)); return panel;
    }
    void showEditor() {
        JPanel actions = buttons();
        actions.add(iconButton("✓", tr("Check syntax", "Проверить синтаксис"), this::check));
        header(editorFile.getFileName().toString(), project.relativize(editorFile).toString(), actions, () -> showFiles(directory)); updateEditorTitle();
        JPanel page = new JPanel(new BorderLayout(0, 8)); page.setBorder(new EmptyBorder(16, 16, 16, 16)); page.setBackground(BG);
        editor.setName("script-editor");
        JPanel editorCard = new Material.Card(new BorderLayout(), 16); editorCard.setBorder(new EmptyBorder(8, 8, 8, 8)); editorCard.add(scroll(editor), BorderLayout.CENTER); page.add(editorCard, BorderLayout.CENTER);
        JPanel bottom = new JPanel(new BorderLayout(0, 8)); bottom.setOpaque(false);
        JPanel controls = new JPanel(new GridLayout(1, 2, 8, 0)); controls.setOpaque(false);
        JButton save = button("✓  " + tr("Save", "Сохранить"), this::save); save.setName("save"); controls.add(save);
        runButton = button("▶  " + tr("Run project", "Запустить проект"), this::runProject); runButton.setName("run"); runButton.setEnabled(!running); controls.add(runButton); stopButton = null;
        bottom.add(controls, BorderLayout.NORTH); bottom.add(console(), BorderLayout.CENTER); page.add(bottom, BorderLayout.SOUTH); content(page);
    }
    private void showRuntime() {
        JPanel actions = buttons();
        stopButton = button(tr("Stop and edit", "Стоп и редактор"), () -> { compiler.stop(); if (editorFile != null) showEditor(); else showFiles(directory); }); actions.add(stopButton);
        header(tr("Run", "Запуск"), project.getFileName().toString(), actions, () -> { compiler.stop(); if (editorFile != null) showEditor(); else showFiles(directory); });
        JPanel page = new JPanel(new BorderLayout()); page.setBackground(BG); page.setBorder(new EmptyBorder(16, 16, 16, 16)); page.add(console(), BorderLayout.SOUTH); content(page);
    }
    boolean save() {
        if (editorFile == null) return true;
        try { store.save(editorFile, editor.getText()); dirty = false; updateEditorTitle(); return true; }
        catch (IOException e) { error(e); return false; }
    }
    private boolean confirmSave() {
        if (!dirty || editorFile == null) return true;
        int result = JOptionPane.showConfirmDialog(this, tr("Save changes?", "Сохранить изменения?"), "LuaC Studio", JOptionPane.YES_NO_CANCEL_OPTION);
        if (result == JOptionPane.CANCEL_OPTION || result == JOptionPane.CLOSED_OPTION) return false;
        if (result == JOptionPane.YES_OPTION) return save();
        dirty = false; return true;
    }
    void check() { if (editorFile != null && (!dirty || save())) execute("--check", editorFile); }
    void runProject() {
        if (project == null || (dirty && !save())) return;
        if (painting != null) { if (!confirmPaint()) return; painting = null; }
        if (!Files.isRegularFile(project.resolve("main.lua"), LinkOption.NOFOLLOW_LINKS)) { error(new IOException(tr("main.lua is missing", "Файл main.lua не найден"))); return; }
        showRuntime(); execute("--jit", Paths.get("main.lua"));
    }
    private void execute(String mode, Path source) {
        output.setText("");
        try {
            compiler.start(project, mode, source, text -> SwingUtilities.invokeLater(() -> {
                output.append(text); if (output.getDocument().getLength() > 200000) output.replaceRange("", 0, output.getDocument().getLength() - 200000);
            }), result -> SwingUtilities.invokeLater(() -> {
                output.append("\n" + tr("Exit code: ", "Код завершения: ") + result + "\n"); running = false; if (runButton != null) runButton.setEnabled(true); if (stopButton != null) stopButton.setEnabled(true);
            }));
            running = true; if (runButton != null) runButton.setEnabled(false); if (stopButton != null) stopButton.setEnabled(true);
        } catch (IOException e) { output.setText(e.getMessage()); }
    }
    private void settings() {
        String[] choices = {tr("Language", "Язык"), tr("Theme color", "Цвет темы"), tr("Close", "Закрыть")};
        int n = JOptionPane.showOptionDialog(this, "LuaC Studio", tr("Settings", "Настройки"), JOptionPane.DEFAULT_OPTION, JOptionPane.PLAIN_MESSAGE, null, choices, choices[0]);
        if (n == 0) { Object lang = JOptionPane.showInputDialog(this, tr("Language", "Язык"), "LuaC Studio", JOptionPane.PLAIN_MESSAGE, null, new String[]{"English", "Русский"}, russian ? "Русский" : "English"); if (lang != null) { russian = lang.equals("Русский"); prefs.putBoolean("russian", russian); showProjects(); } }
        if (n == 1) { Color c = JColorChooser.showDialog(this, tr("Theme color", "Цвет темы"), accent); if (c != null) { accent = c; prefs.putInt("accent", c.getRGB()); showProjects(); } }
    }
    private void setupShortcuts() {
        JRootPane r = getRootPane(); InputMap map = r.getInputMap(JComponent.WHEN_IN_FOCUSED_WINDOW); ActionMap actions = r.getActionMap();
        map.put(KeyStroke.getKeyStroke("control S"), "save"); actions.put("save", new AbstractAction() { public void actionPerformed(ActionEvent e) { save(); } });
        map.put(KeyStroke.getKeyStroke("F5"), "run"); actions.put("run", new AbstractAction() { public void actionPerformed(ActionEvent e) { runProject(); } });
        map.put(KeyStroke.getKeyStroke("F6"), "check"); actions.put("check", new AbstractAction() { public void actionPerformed(ActionEvent e) { check(); } });
    }
    static Path appHome() {
        String override = System.getProperty("luac.home");
        if (override != null && !override.isEmpty()) return Paths.get(override);
        String launcher = System.getProperty("jpackage.app-path");
        if (launcher != null && !launcher.isEmpty()) return Paths.get(launcher).toAbsolutePath().getParent();
        try {
            Path jar = Paths.get(Studio.class.getProtectionDomain().getCodeSource().getLocation().toURI());
            if (Files.isRegularFile(jar) && jar.getParent() != null && jar.getParent().getFileName().toString().equals("app")) return jar.getParent().getParent();
        } catch (Exception ignored) { }
        return Paths.get(System.getProperty("user.dir"));
    }
    static Path projectsRoot() {
        String appdata = System.getenv("APPDATA");
        Path home = System.getProperty("os.name").toLowerCase(Locale.ROOT).contains("win") && appdata != null && !appdata.isEmpty() ? Paths.get(appdata) : Paths.get(System.getProperty("user.home"));
        return home.resolve("LuaC").resolve("projects");
    }
    public static void main(String[] args) {
        SwingUtilities.invokeLater(() -> {
            try {
                Path home = appHome();
                Studio studio = new Studio(projectsRoot(), home);
                studio.setVisible(true);
                // Used by the package test to check the real launcher and its home.
                if (args.length == 2 && args[0].equals("--launcher-test")) {
                    if (!studio.isShowing()) throw new IOException("Window was not shown");
                    Files.writeString(Paths.get(args[1]), home.toAbsolutePath().normalize().toString());
                }
            } catch (Exception e) { e.printStackTrace(); JOptionPane.showMessageDialog(null, e.toString(), "LuaC Studio", JOptionPane.ERROR_MESSAGE); }
        });
    }
}
