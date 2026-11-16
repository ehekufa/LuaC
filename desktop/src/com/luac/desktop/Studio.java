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
    private static final Color BG = new Color(18, 20, 29), PANEL = new Color(35, 38, 49), TEXT = new Color(239, 239, 244), MUTED = new Color(180, 181, 192);
    private final Preferences prefs = Preferences.userNodeForPackage(Studio.class);
    final ProjectStore store;
    final Compiler compiler;
    Path project, directory, editorFile;
    final JTextArea editor = new JTextArea(), output = new JTextArea();
    final JPanel body = new JPanel(new BorderLayout()), bar = new JPanel(new BorderLayout(8, 0));
    final JLabel title = new JLabel();
    boolean dirty, loading, running, russian;
    Color accent;
    JButton runButton, stopButton;

    Studio(Path root, Path home) throws IOException {
        super("LuaC Studio");
        java.net.URL logo = Studio.class.getResource("logo.png");
        if (logo != null) setIconImage(new ImageIcon(logo).getImage());
        store = new ProjectStore(root); compiler = new Compiler(home);
        russian = prefs.getBoolean("russian", Locale.getDefault().getLanguage().equals("ru"));
        accent = new Color(prefs.getInt("accent", 0xff715bd4));
        setDefaultCloseOperation(DO_NOTHING_ON_CLOSE);
        addWindowListener(new WindowAdapter() { @Override public void windowClosing(WindowEvent e) { if (confirmSave()) { compiler.stop(); dispose(); } } });
        setMinimumSize(new Dimension(720, 500)); setSize(1050, 760); setLocationRelativeTo(null);
        getContentPane().setBackground(BG); getContentPane().setLayout(new BorderLayout());
        bar.setBackground(PANEL); bar.setBorder(new EmptyBorder(12, 16, 12, 16));
        title.setForeground(TEXT); title.setFont(title.getFont().deriveFont(Font.BOLD, 21f)); bar.add(title, BorderLayout.CENTER);
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
        JButton b = new JButton(label); b.setFocusable(false); b.setBackground(accent); b.setForeground(Color.WHITE);
        b.setBorder(new EmptyBorder(9, 14, 9, 14)); b.addActionListener(e -> action.run()); return b;
    }
    private JPanel buttons() { JPanel p = new JPanel(new FlowLayout(FlowLayout.RIGHT, 8, 0)); p.setOpaque(false); return p; }
    private void header(String name, JPanel actions) {
        title.setText(name); bar.removeAll(); bar.add(title, BorderLayout.CENTER); bar.add(actions, BorderLayout.EAST); bar.revalidate(); bar.repaint();
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
    private JPanel row(String name, String subtitle, Runnable open, JButton... actions) {
        JPanel p = new JPanel(new BorderLayout(12, 0)); p.setAlignmentX(Component.LEFT_ALIGNMENT); p.setBackground(PANEL);
        p.setBorder(new EmptyBorder(12, 14, 12, 14)); p.setMaximumSize(new Dimension(Integer.MAX_VALUE, 80));
        JPanel labels = new JPanel(new GridLayout(0, 1, 0, 3)); labels.setOpaque(false);
        JLabel main = new JLabel(name), sub = new JLabel(subtitle); main.setForeground(TEXT); main.setFont(main.getFont().deriveFont(Font.BOLD, 16f)); sub.setForeground(MUTED);
        labels.add(main); labels.add(sub); labels.setCursor(Cursor.getPredefinedCursor(Cursor.HAND_CURSOR));
        labels.addMouseListener(new MouseAdapter() { @Override public void mouseClicked(MouseEvent e) { open.run(); } });
        p.add(labels, BorderLayout.CENTER); JPanel group = buttons(); for (JButton b : actions) group.add(b); p.add(group, BorderLayout.EAST); return p;
    }
    void showProjects() {
        if (editorFile != null && !confirmSave()) return;
        editorFile = null; project = null; directory = null;
        JPanel actions = buttons(); actions.add(button(tr("Settings", "Настройки"), this::settings));
        actions.add(button(tr("+ New project", "+ Новый проект"), () -> {
            String name = ask(tr("Project name", "Название проекта"), ""); if (name == null) return;
            try { showFiles(store.createProject(name)); } catch (IOException e) { error(e); }
        }));
        header("LuaC Studio", actions); JPanel list = listPanel();
        try {
            for (Path p : store.projects()) {
                JButton open = button(tr("Open", "Открыть"), () -> showFiles(p));
                JButton run = button(tr("Run", "Запуск"), () -> { showFiles(p); runProject(); });
                JButton more = button("⋮", () -> menu(p, true));
                list.add(row(p.getFileName().toString(), tr("LuaC project", "Проект LuaC"), () -> showFiles(p), open, run, more)); list.add(Box.createVerticalStrut(12));
            }
        } catch (IOException e) { error(e); }
        content(scroll(list));
    }
    void showFiles(Path path) {
        if (editorFile != null && !confirmSave()) return;
        editorFile = null;
        if (project == null || !path.startsWith(project)) project = path;
        directory = path;
        JPanel actions = buttons();
        actions.add(button(tr("Back", "Назад"), () -> { if (directory.equals(project)) showProjects(); else showFiles(directory.getParent()); }));
        actions.add(button(tr("+ File", "+ Файл"), () -> create(false)));
        actions.add(button(tr("+ Folder", "+ Папка"), () -> create(true)));
        actions.add(button(tr("Import", "Импорт"), this::importFile));
        runButton = button(tr("Run", "Запуск"), this::runProject); runButton.setEnabled(!running); actions.add(runButton);
        stopButton = button(tr("Stop", "Стоп"), compiler::stop); stopButton.setEnabled(running); actions.add(stopButton);
        header(project.getFileName() + "  /  " + (directory.equals(project) ? tr("Project files", "Файлы проекта") : project.relativize(directory).toString()), actions);
        JPanel list = listPanel();
        try {
            for (Path p : store.children(directory)) {
                boolean folder = Files.isDirectory(p, LinkOption.NOFOLLOW_LINKS);
                Runnable open = () -> { if (folder) showFiles(p); else openFile(p); };
                list.add(row((folder ? "▸  " : "     ") + p.getFileName(), folder ? tr("Folder", "Папка") : tr("File", "Файл"), open,
                    button(tr("Open", "Открыть"), open), button("⋮", () -> menu(p, false))));
                list.add(Box.createVerticalStrut(8));
            }
        } catch (IOException e) { error(e); }
        JSplitPane split = new JSplitPane(JSplitPane.VERTICAL_SPLIT, scroll(list), console());
        split.setResizeWeight(1); split.setDividerSize(6); split.setBorder(null); content(split);
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
    private void menu(Path path, boolean rootProject) {
        String[] options = {tr("Rename", "Переименовать"), tr("Delete", "Удалить"), tr("Cancel", "Отмена")};
        int choice = JOptionPane.showOptionDialog(this, path.getFileName(), "LuaC Studio", JOptionPane.DEFAULT_OPTION, JOptionPane.QUESTION_MESSAGE, null, options, options[0]);
        try {
            if (choice == 0) {
                String name = ask(tr("New name (references are not updated)", "Новое имя (пути в коде не изменятся)"), path.getFileName().toString());
                if (name != null) store.rename(path, name);
            } else if (choice == 1 && JOptionPane.showConfirmDialog(this, tr("Delete permanently: ", "Удалить безвозвратно: ") + path.getFileName() + "?", "LuaC Studio", JOptionPane.YES_NO_OPTION) == JOptionPane.YES_OPTION) store.delete(path);
            if (rootProject) showProjects(); else showFiles(directory);
        } catch (IOException e) { error(e); }
    }
    void openFile(Path file) {
        String name = file.getFileName().toString().toLowerCase(Locale.ROOT);
        if (!(name.endsWith(".lua") || name.endsWith(".txt"))) {
            JOptionPane.showMessageDialog(this, tr("Only Lua and text files are editable here.", "Здесь можно редактировать только Lua и текстовые файлы.")); return;
        }
        try {
            String text = store.read(file); loading = true; editor.setText(text); editor.setCaretPosition(0); loading = false; dirty = false; editorFile = file;
            showEditor();
        } catch (IOException e) { loading = false; error(e); }
    }
    private void updateEditorTitle() { if (editorFile != null) title.setText(editorFile.getFileName().toString() + (dirty ? " *" : "")); }
    private JPanel console() {
        JPanel panel = new JPanel(new BorderLayout()); panel.setBackground(PANEL); panel.setBorder(new EmptyBorder(8, 16, 8, 16));
        JLabel label = new JLabel(tr("CONSOLE", "КОНСОЛЬ")); label.setForeground(MUTED); panel.add(label, BorderLayout.NORTH); panel.add(scroll(output), BorderLayout.CENTER); return panel;
    }
    void showEditor() {
        JPanel actions = buttons(); actions.add(button(tr("Back", "Назад"), () -> showFiles(directory)));
        actions.add(button(tr("Check", "Проверить"), this::check));
        actions.add(button(tr("Save", "Сохранить"), this::save));
        runButton = button(tr("Run", "Запуск"), this::runProject); runButton.setEnabled(!running); actions.add(runButton);
        stopButton = button(tr("Stop", "Стоп"), compiler::stop); stopButton.setEnabled(running); actions.add(stopButton);
        header(editorFile.getFileName().toString(), actions); updateEditorTitle();
        JPanel panel = new JPanel(new BorderLayout(0, 8)); panel.setBorder(new EmptyBorder(16, 16, 16, 16)); panel.setBackground(BG);
        JSplitPane split = new JSplitPane(JSplitPane.VERTICAL_SPLIT, scroll(editor), console()); split.setResizeWeight(.8); split.setDividerSize(6); split.setBorder(null);
        panel.add(split, BorderLayout.CENTER); content(panel);
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
        if (!Files.isRegularFile(project.resolve("main.lua"), LinkOption.NOFOLLOW_LINKS)) { error(new IOException(tr("main.lua is missing", "Файл main.lua не найден"))); return; }
        execute("--jit", Paths.get("main.lua"));
    }
    private void execute(String mode, Path source) {
        output.setText("");
        try {
            compiler.start(project, mode, source, text -> SwingUtilities.invokeLater(() -> {
                output.append(text); if (output.getDocument().getLength() > 200000) output.replaceRange("", 0, output.getDocument().getLength() - 200000);
            }), result -> SwingUtilities.invokeLater(() -> {
                output.append("\n" + tr("Exit code: ", "Код завершения: ") + result + "\n"); running = false; if (runButton != null) runButton.setEnabled(true); if (stopButton != null) stopButton.setEnabled(false);
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
        SwingUtilities.invokeLater(() -> { try { new Studio(projectsRoot(), appHome()).setVisible(true); } catch (Exception e) { e.printStackTrace(); JOptionPane.showMessageDialog(null, e.toString(), "LuaC Studio", JOptionPane.ERROR_MESSAGE); } });
    }
}
