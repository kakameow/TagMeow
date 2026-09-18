package com.example.tagmeow;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.webkit.MimeTypeMap;

import java.io.ByteArrayOutputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;

// StorageAccess 的 Android SAF 实现

// 基于 ContentResolver + DocumentsContract 直接操作 DocumentProvider
// 不引入 androidx.documentfile 依赖

// root 使用 ACTION_OPEN_DOCUMENT_TREE 得到的 tree Uri
// 调用方需要在 Activity 中先 takePersistableUriPermission 并持久化授权

// 职责限制在存储层：
// - 不解释标签格式
// - 不接触 SQLite

public final class SafStorageAccess implements StorageAccess {

    private static final String[] PROJECTION = {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_LAST_MODIFIED,
            DocumentsContract.Document.COLUMN_SIZE,
    };

    private static final String DEFAULT_MIME_TYPE = "application/octet-stream";
    // 原子写入使用的临时文档后缀
    private static final String TEMP_SUFFIX = ".tmp";

    // 一个受管理 root
    private static final class RootEntry {

        final Uri tree_uri;
        final String tree_document_id;
        final Uri tree_document_uri;

        RootEntry(Uri tree_uri) {
            this.tree_uri = tree_uri;
            this.tree_document_id = DocumentsContract.getTreeDocumentId(tree_uri);
            this.tree_document_uri = DocumentsContract.buildDocumentUriUsingTree(tree_uri, tree_document_id);
        }
    }

    // 单个文档的缓存信息
    private static final class DocInfo {

        final RootEntry root;
        final Uri uri;
        final String document_id;
        final String name;
        final String mime_type;
        final boolean directory;
        final long last_modified;
        final long size;

        DocInfo(RootEntry root, Uri uri, String document_id, String name, String mime_type, long last_modified, long size) {

            this.root = root;
            this.uri = uri;
            this.document_id = document_id;
            this.name = name == null ? "" : name;
            this.mime_type = mime_type == null ? "" : mime_type;
            this.directory = DocumentsContract.Document.MIME_TYPE_DIR.equals(mime_type);
            this.last_modified = last_modified;
            this.size = size;
        }
    }

    private final ContentResolver resolver;
    private final Map<String, RootEntry> roots = new HashMap<>();

    // 目录 documentUri -> (子项名称 -> 子项信息)
    private final Map<String, Map<String, DocInfo>> listings = new HashMap<>();

    // 最后一次失败的原因（provider 抛出来的异常信息）
    // 以前这些异常全被吞掉 于是「授权丢了」和「空目录」在上层看起来一模一样
    private String error_string = "";

    public SafStorageAccess(Context context) {
        this.resolver = Objects.requireNonNull(context).getContentResolver();
    }

    @Override
    public synchronized void addRoot(String root_id, String locator) {

        Objects.requireNonNull(root_id);
        Objects.requireNonNull(locator);

        listings.clear();

        try {
            roots.put(root_id, new RootEntry(Uri.parse(locator)));
        } catch (IllegalArgumentException error) {
            // 不是合法的 tree Uri 时视为未注册
            error_string = "invalid tree uri: " + locator;
            roots.remove(root_id);
        }
    }

    @Override
    public synchronized void removeRoot(String root_id) {
        roots.remove(root_id);
        listings.clear();
    }

    @Override
    public synchronized boolean exists(FileRef ref) {
        return resolveInfo(ref) != null;
    }

    @Override
    public synchronized boolean isDirectory(FileRef ref) {
        DocInfo info = resolveInfo(ref);
        return info != null && info.directory;
    }

    @Override
    public synchronized List<FileRef> listChildren(FileRef directory) throws IOException {
        List<FileRef> result = new ArrayList<>();

        DocInfo info = resolveInfo(directory);
        if (info == null) {
            // 读不到就抛：返回空表的话上层分不清「空目录」和「没有权限」
            throw new IOException("cannot access directory: " + directory + errorSuffix());
        }

        if (!info.directory) {
            return result;
        }

        Map<String, DocInfo> children = listOf(info);
        if (children == null) {
            throw new IOException("cannot list directory: " + directory + errorSuffix());
        }

        List<String> names = new ArrayList<>(children.keySet());
        Collections.sort(names);

        for (String name : names) {
            result.add(directory.child(name));
        }

        return result;
    }

    @Override
    public synchronized byte[] readAll(FileRef ref) throws IOException {
        DocInfo info = resolveInfo(ref);
        if (info == null) {
            throw new IOException("document not found: " + ref);
        }

        try (InputStream input = resolver.openInputStream(info.uri)) {
            if (input == null) {
                throw new IOException("cannot open input stream: " + ref);
            }

            ByteArrayOutputStream buffer = new ByteArrayOutputStream();
            byte[] chunk = new byte[8192];
            int read;

            while ((read = input.read(chunk)) > 0) {
                buffer.write(chunk, 0, read);
            }

            return buffer.toByteArray();
        }
    }

    @Override
    public synchronized void writeAll(FileRef ref, byte[] data, boolean atomic) throws IOException {

        Objects.requireNonNull(ref);
        Objects.requireNonNull(data);

        if (ref.isRoot()) {
            throw new IOException("cannot write root directory: " + ref);
        }

        String mime_type = mimeTypeOf(ref.getName());

        if (!atomic) {
            writeTo(ensureDocument(ref, mime_type), data);
            return;
        }

        // 先写临时文档 再改名覆盖

        FileRef temp_ref = ref.getParent().child(ref.getName() + TEMP_SUFFIX);
        Uri temp_uri = createDocument(temp_ref, mimeTypeOf(temp_ref.getName()));

        if (temp_uri == null) {
            // 无法创建临时文档时退回直接写入
            writeTo(ensureDocument(ref, mime_type), data);
            return;
        }

        try {
            writeTo(temp_uri, data);
        } catch (IOException error) {
            deleteQuietly(temp_uri);
            throw error;
        }

        DocInfo existing = resolveInfo(ref);
        if (existing != null) {
            // 先删掉旧文档 把最终名字腾出来
            deleteQuietly(existing.uri);
        }

        Uri renamed = renameQuietly(temp_uri, ref.getName());

        // 清掉缓存后按目标路径重新解析
        // 只有真的存在同名文档才算改名成功
        DocInfo after = resolveInfo(ref);

        if (after != null && ref.getName().equals(after.name)) {
            return;
        }

        // Provider 不支持改名 或者改名后被改成了别的名字：清理后直接写入目标
        if (renamed != null) {
            deleteQuietly(renamed);
        } else {
            deleteQuietly(temp_uri);
        }

        writeTo(ensureDocument(ref, mime_type), data);
    }

    @Override
    public synchronized boolean createDirectories(FileRef directory) {
        RootEntry root = roots.get(directory.getRootId());
        if (root == null) {
            return false;
        }

        String relative = directory.getRelativePath();
        if (relative.isEmpty()) {
            return true;
        }

        Uri current_uri = root.tree_document_uri;
        String current_id = root.tree_document_id;

        for (String segment : relative.split("/")) {
            if (segment.isEmpty()) {
                continue;
            }

            DocInfo child = childOf(root, current_uri, current_id, segment);

            if (child == null) {
                Uri created;
                try {
                    created = DocumentsContract.createDocument(resolver, current_uri, DocumentsContract.Document.MIME_TYPE_DIR, segment);
                } catch (FileNotFoundException error) {
                    return false;
                }

                if (created == null) {
                    return false;
                }

                current_uri = created;
                current_id = DocumentsContract.getDocumentId(created);
            } else {
                if (!child.directory) {
                    return false;
                }

                current_uri = child.uri;
                current_id = child.document_id;
            }
        }

        listings.clear();
        return true;
    }

    @Override
    public synchronized boolean delete(FileRef ref) {
        if (ref.isRoot()) {
            return false;
        }

        DocInfo info = resolveInfo(ref);
        if (info == null) {
            return false;
        }

        boolean deleted;
        try {
            deleted = DocumentsContract.deleteDocument(resolver, info.uri);
        } catch (FileNotFoundException error) {
            return false;
        }

        listings.clear();
        return deleted;
    }

    @Override
    public synchronized boolean rename(FileRef ref, String new_name) {

        Objects.requireNonNull(new_name);

        if (ref.isRoot() || new_name.isEmpty() || new_name.indexOf('/') >= 0) {
            return false;
        }

        DocInfo info = resolveInfo(ref);
        if (info == null) {
            return false;
        }

        if (new_name.equals(info.name)) {
            return true;
        }

        Uri renamed;
        try {
            renamed = DocumentsContract.renameDocument(resolver, info.uri, new_name);
        } catch (FileNotFoundException error) {
            return false;
        }

        listings.clear();
        return renamed != null;
    }

    @Override
    public synchronized long lastModified(FileRef ref) {
        DocInfo info = resolveInfo(ref);
        return info == null ? 0L : info.last_modified;
    }

    @Override
    public synchronized long size(FileRef ref) {
        DocInfo info = resolveInfo(ref);
        if (info == null || info.directory) {
            return 0L;
        }

        return info.size;
    }

    @Override
    public synchronized String locatorOf(FileRef ref) {
        DocInfo info = resolveInfo(ref);
        return info == null ? null : info.uri.toString();
    }

    @Override
    public FileRef childOf(FileRef directory, String name) {

        return directory.child(name);
    }

    // 解析 FileRef 为文档信息 root 未注册 或者路径中任意一段不存在时返回 null
    private DocInfo resolveInfo(FileRef ref) {
        if (ref == null) {
            return null;
        }

        RootEntry root = roots.get(ref.getRootId());
        if (root == null) {
            return null;
        }

        String relative = ref.getRelativePath();
        if (relative.isEmpty()) {
            // root 本身也要真去问一次 provider
            // 以前这里凭空造一个「目录存在」的结果：授权被系统回收之后
            // exists(root) 依然返回 true，目录看着是好的，扫描却一条都收不到，
            // 最后变成「添加成功 + 索引为空」而且一句错误都没有
            if (!probeRoot(root)) {
                return null;
            }

            return new DocInfo(
                    root,
                    root.tree_document_uri,
                    root.tree_document_id,
                    Uri.decode(root.tree_document_id),
                    DocumentsContract.Document.MIME_TYPE_DIR,
                    0L,
                    0L);
        }

        Uri current_uri = root.tree_document_uri;
        String current_id = root.tree_document_id;
        DocInfo current = null;

        for (String segment : relative.split("/")) {
            if (segment.isEmpty()) {
                continue;
            }

            current = childOf(root, current_uri, current_id, segment);
            if (current == null) {
                return null;
            }

            current_uri = current.uri;
            current_id = current.document_id;
        }

        return current;
    }

    // 真去列一次 root 的子项 确认现在还读得到（结果会进 listings 缓存）
    // 以前 resolveInfo() 对 root 是凭空造的：授权被回收之后 exists(root) 依然是 true
    private boolean probeRoot(RootEntry root) {
        return queryChildren(root, root.tree_document_uri, root.tree_document_id) != null;
    }

    // 把最后一次失败的原因拼成一小段给人看的信息
    private String errorSuffix() {
        return error_string.isEmpty() ? "" : " (" + error_string + ")";
    }

    // 最后一次失败的原因 供上层显示
    @Override
    public synchronized String getLastError() {
        return error_string;
    }

    // 从目录列表缓存中查找子项
    private DocInfo childOf(RootEntry root, Uri parent_uri, String parent_document_id, String name) {

        Map<String, DocInfo> children = listings.get(parent_uri.toString());

        if (children == null) {
            children = queryChildren(root, parent_uri, parent_document_id);
            if (children == null) {
                return null;
            }
        }

        return children.get(name);
    }

    private Map<String, DocInfo> listOf(DocInfo directory) {
        Map<String, DocInfo> cached = listings.get(directory.uri.toString());
        if (cached != null) {
            return cached;
        }

        return queryChildren(directory.root, directory.uri, directory.document_id);
    }

    // 查询目录子项 并写入缓存
    private Map<String, DocInfo> queryChildren(RootEntry root, Uri parent_uri, String parent_document_id) {

        Uri children_uri = DocumentsContract.buildChildDocumentsUriUsingTree(root.tree_uri, parent_document_id);
        Map<String, DocInfo> children = new LinkedHashMap<>();

        try (Cursor cursor = resolver.query(children_uri, PROJECTION, null, null, null)) {
            if (cursor == null) {
                return null;
            }

            while (cursor.moveToNext()) {
                String document_id = cursor.getString(0);
                String name = cursor.getString(1);
                String mime_type = cursor.getString(2);
                long last_modified = cursor.isNull(3) ? 0L : cursor.getLong(3);
                long size = cursor.isNull(4) ? 0L : cursor.getLong(4);

                if (document_id == null || name == null) {
                    continue;
                }

                children.put(
                        name,
                        new DocInfo(
                                root,
                                DocumentsContract.buildDocumentUriUsingTree(root.tree_uri, document_id),
                                document_id,
                                name,
                                mime_type,
                                last_modified,
                                size));
            }
        } catch (Exception error) {
            // provider 抛异常（没有授权 / document id 失效 / 目录被删）
            // 记下来再返回 null：以前这里什么都不说 上层只能看到空目录
            error_string = "query failed: " + error;
            return null;
        }

        error_string = "";
        listings.put(parent_uri.toString(), children);
        return children;
    }

    // 在父目录中创建文档
    // 返回新文档 Uri 失败返回 null
    private Uri createDocument(FileRef ref, String mime_type) {

        if (ref.isRoot()) {
            return null;
        }

        FileRef parent = ref.getParent();
        DocInfo parent_info = resolveInfo(parent);

        if (parent_info == null || !parent_info.directory) {
            return null;
        }

        Uri created;
        try {
            created = DocumentsContract.createDocument(
                    resolver,
                    parent_info.uri,
                    mime_type,
                    ref.getName());
        } catch (FileNotFoundException error) {
            return null;
        }

        listings.clear();
        return created;
    }

    // 目标不存在时创建 返回可以写入的文档 Uri
    private Uri ensureDocument(FileRef ref, String mime_type) throws IOException {

        DocInfo existing = resolveInfo(ref);
        if (existing != null) {
            return existing.uri;
        }

        Uri created = createDocument(ref, mime_type);
        if (created == null) {
            throw new IOException("cannot create document: " + ref);
        }

        return created;
    }

    // 删除文档 失败或不存在都不抛异常
    private void deleteQuietly(Uri document_uri) {
        if (document_uri == null) {
            return;
        }

        try {
            DocumentsContract.deleteDocument(resolver, document_uri);
        } catch (FileNotFoundException error) {
            // 已经不存在 可以忽略
        }

        listings.clear();
    }

    // 改名 失败返回 null
    private Uri renameQuietly(Uri document_uri, String new_name) {

        Uri renamed;

        try {
            renamed = DocumentsContract.renameDocument(resolver, document_uri, new_name);
        } catch (FileNotFoundException error) {
            renamed = null;
        }

        listings.clear();
        return renamed;
    }

    private static String documentIdOf(Uri document_uri) {
        try {
            return DocumentsContract.getDocumentId(document_uri);
        } catch (IllegalArgumentException error) {
            return "";
        }
    }

    private void writeTo(Uri document_uri, byte[] data) throws IOException {

        if (document_uri == null) {
            throw new IOException("document uri is null");
        }

        try (OutputStream output = resolver.openOutputStream(document_uri, "wt")) {
            if (output == null) {
                throw new IOException("cannot open output stream: " + document_uri);
            }

            output.write(data);
            output.flush();
        }
    }

    // 根据文件名推测 MIME 类型
    public static String mimeTypeOf(String file_name) {
        int dot = file_name.lastIndexOf('.');
        if (dot < 0 || dot == file_name.length() - 1) {
            return DEFAULT_MIME_TYPE;
        }

        String extension = file_name.substring(dot + 1).toLowerCase();
        String mime_type = MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension);

        return mime_type == null ? DEFAULT_MIME_TYPE : mime_type;
    }
}