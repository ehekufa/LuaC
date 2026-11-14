package com.luac.studio;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import org.json.JSONTokener;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Base64;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/** The .luac project interchange format. Files are binary-safe Base64 inside JSON. */
final class ProjectArchive {
    private static final String FORMAT = "luac-project";
    private static final int VERSION = 1;
    private static final int MAX_ENTRIES = 1024;
    private static final int MAX_FILE_BYTES = 8 * 1024 * 1024;
    private static final int MAX_TOTAL_BYTES = 16 * 1024 * 1024;
    private static final int MAX_JSON_BYTES = 24 * 1024 * 1024;
    private static final int MAX_DEPTH = 48;

    static final class Entry {
        final String path;
        final byte[] bytes;
        Entry(String path, byte[] bytes) { this.path = path; this.bytes = bytes; }
    }

    static final class Document {
        final String name;
        final List<String> directories = new ArrayList<>();
        final List<Entry> files = new ArrayList<>();
        Document(String name) { this.name = name; }
    }

    private ProjectArchive() { }

    static void write(File project, OutputStream output) throws IOException {
        if (!project.isDirectory() || Files.isSymbolicLink(project.toPath()))
            throw new IOException("Project is not a normal directory");
        checkPath(project.getName());
        Document document = new Document(project.getName());
        collect(project, "", document, new long[] {0});
        try {
            JSONObject json = new JSONObject();
            json.put("format", FORMAT);
            json.put("version", VERSION);
            json.put("name", document.name);
            JSONArray directories = new JSONArray();
            for (String path : document.directories) directories.put(path);
            json.put("directories", directories);
            JSONArray files = new JSONArray();
            for (Entry entry : document.files) {
                JSONObject file = new JSONObject();
                file.put("path", entry.path);
                file.put("data", Base64.getEncoder().encodeToString(entry.bytes));
                files.put(file);
            }
            json.put("files", files);
            byte[] bytes = json.toString().getBytes(StandardCharsets.UTF_8);
            if (bytes.length > MAX_JSON_BYTES) throw new IOException("Project archive is too large");
            output.write(bytes);
            output.flush();
        } catch (JSONException error) {
            throw new IOException("Cannot create project archive", error);
        }
    }

    private static void collect(File directory, String prefix, Document document, long[] total) throws IOException {
        File[] children = directory.listFiles();
        if (children == null) throw new IOException("Cannot list " + directory.getName());
        Arrays.sort(children, Comparator.comparing(File::getName));
        for (File child : children) {
            String path = prefix.isEmpty() ? child.getName() : prefix + "/" + child.getName();
            checkPath(path);
            if (Files.isSymbolicLink(child.toPath())) throw new IOException("Symbolic links cannot be exported: " + path);
            if (document.directories.size() + document.files.size() >= MAX_ENTRIES)
                throw new IOException("Too many project entries");
            if (child.isDirectory()) {
                document.directories.add(path);
                collect(child, path, document, total);
            } else if (child.isFile()) {
                try (InputStream input = new FileInputStream(child)) {
                    byte[] bytes = readBounded(input, MAX_FILE_BYTES);
                    total[0] += bytes.length;
                    if (total[0] > MAX_TOTAL_BYTES) throw new IOException("Project is too large");
                    document.files.add(new Entry(path, bytes));
                }
            } else throw new IOException("Unsupported file: " + path);
        }
    }

    static Document read(InputStream input) throws IOException {
        byte[] bytes = readBounded(input, MAX_JSON_BYTES);
        final String text;
        try {
            text = StandardCharsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(bytes)).toString();
        } catch (CharacterCodingException error) {
            throw new IOException("Project archive is not valid UTF-8", error);
        }
        try {
            JSONTokener parser = new JSONTokener(text);
            Object parsed = parser.nextValue();
            if (!(parsed instanceof JSONObject) || parser.nextClean() != 0)
                throw new IOException("Invalid .luac JSON");
            JSONObject json = (JSONObject) parsed;
            Object format = json.opt("format"), version = json.opt("version");
            if (!FORMAT.equals(format) || !(version instanceof Number) ||
                ((Number)version).doubleValue() != VERSION)
                throw new IOException("Unsupported .luac project format or version");
            Object rawName = json.opt("name");
            if (!(rawName instanceof String) || ((String) rawName).trim().isEmpty())
                throw new IOException("Project name is missing");
            String name = ((String) rawName).trim();
            checkPath(name);
            if (name.indexOf('/') >= 0) throw new IOException("Invalid project name");
            JSONArray directories = json.optJSONArray("directories");
            JSONArray files = json.optJSONArray("files");
            if (directories == null || files == null) throw new IOException("Project directories or files are missing");
            if ((long)directories.length() + files.length() > MAX_ENTRIES) throw new IOException("Too many project entries");
            Document document = new Document(name);
            Set<String> paths = new HashSet<>();
            Set<String> filePaths = new HashSet<>();
            for (int i = 0; i < directories.length(); i++) {
                Object raw = directories.get(i);
                if (!(raw instanceof String)) throw new IOException("Invalid directory entry");
                String path = (String) raw;
                checkPath(path);
                if (!paths.add(path)) throw new IOException("Duplicate path: " + path);
                document.directories.add(path);
            }
            long total = 0;
            for (int i = 0; i < files.length(); i++) {
                Object raw = files.get(i);
                if (!(raw instanceof JSONObject)) throw new IOException("Invalid file entry");
                JSONObject file = (JSONObject) raw;
                Object rawPath = file.opt("path"), rawData = file.opt("data");
                if (!(rawPath instanceof String) || !(rawData instanceof String)) throw new IOException("Invalid file entry");
                String path = (String) rawPath, encoded = (String) rawData;
                checkPath(path);
                if (!paths.add(path)) throw new IOException("Duplicate path: " + path);
                if (encoded.length() > ((MAX_FILE_BYTES + 2L) / 3L) * 4L) throw new IOException("File is too large: " + path);
                byte[] data;
                try { data = Base64.getDecoder().decode(encoded); }
                catch (IllegalArgumentException error) { throw new IOException("Invalid Base64: " + path, error); }
                if (data.length > MAX_FILE_BYTES || (total += data.length) > MAX_TOTAL_BYTES)
                    throw new IOException("Project is too large");
                filePaths.add(path);
                document.files.add(new Entry(path, data));
            }
            for (String path : paths) {
                int slash = path.indexOf('/');
                while (slash >= 0) {
                    if (filePaths.contains(path.substring(0, slash))) throw new IOException("File used as a directory: " + path);
                    slash = path.indexOf('/', slash + 1);
                }
            }
            return document;
        } catch (JSONException error) {
            throw new IOException("Invalid .luac JSON", error);
        }
    }

    private static void checkPath(String path) throws IOException {
        if (path.isEmpty() || path.length() > 4096 || path.startsWith("/") || path.indexOf('\\') >= 0 || path.indexOf(':') >= 0 || path.indexOf('\0') >= 0)
            throw new IOException("Invalid project path");
        String[] components = path.split("/", -1);
        if (components.length > MAX_DEPTH) throw new IOException("Project path is too deep");
        for (String component : components) {
            if (component.isEmpty() || component.equals(".") || component.equals(".."))
                throw new IOException("Invalid project path: " + path);
        }
    }

    private static byte[] readBounded(InputStream input, int limit) throws IOException {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] buffer = new byte[16384];
        int count, total = 0;
        while ((count = input.read(buffer)) != -1) {
            if (count == 0) continue;
            if (count > limit - total) throw new IOException("Project archive exceeds size limit");
            output.write(buffer, 0, count);
            total += count;
        }
        return output.toByteArray();
    }
}
