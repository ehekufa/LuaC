package com.luac.studio;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

/** Install a validated archive into a newly-created private directory. */
final class ProjectFiles {
    private ProjectFiles() { }

    static void write(ProjectArchive.Document document, File root) throws IOException {
        if (!root.isDirectory()) throw new IOException("Project destination is not a directory");
        for (String path : document.directories) {
            File directory = resolve(root, path);
            if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException("Cannot create " + path);
        }
        for (ProjectArchive.Entry entry : document.files) {
            File file = resolve(root, entry.path);
            File parent = file.getParentFile();
            if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create parent for " + entry.path);
            if (!file.createNewFile()) throw new IOException("Duplicate file: " + entry.path);
            try (FileOutputStream out = new FileOutputStream(file)) { out.write(entry.bytes); }
        }
    }

    private static File resolve(File root, String path) throws IOException {
        File file = new File(root, path);
        if (!file.getCanonicalPath().startsWith(root.getCanonicalPath() + File.separator))
            throw new IOException("Path leaves the project");
        return file;
    }

    static void delete(File file) throws IOException {
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children == null) throw new IOException("Cannot list " + file);
            for (File child : children) delete(child);
        }
        if (!file.delete()) throw new IOException("Cannot delete " + file);
    }
}
