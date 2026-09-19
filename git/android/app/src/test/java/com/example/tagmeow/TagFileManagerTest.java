package com.example.tagmeow;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

// TagFileManager 的 JVM 单元测试
// 使用 StorageAccess 所以不需要 Android 运行时
public class TagFileManagerTest {

    @Rule
    public TemporaryFolder folder = new TemporaryFolder();

    private File root;

    private String root_id;

    private StorageAccess storage;

    @Before
    public void setUp() throws Exception {
        root = folder.newFolder("managed");
        root_id = StorageAccess.normalizePath(root.getAbsolutePath());
        storage = new StorageAccess(root);
    }

    private FileRef ref(String relative) {
        return new FileRef(root_id, relative);
    }

    private File createFile(String relative, String content) throws Exception {
        File file = new File(root, relative);
        File parent = file.getParentFile();
        if (parent != null && !parent.exists()) {
            parent.mkdirs();
        }

        Files.write(file.toPath(), content.getBytes(StandardCharsets.UTF_8));
        return file;
    }

    // 文件名格式

    @Test
    public void parseFromFilename_readsTagBlock() {
        assertEquals(
                Arrays.asList("a", "b"),
                TagFileManager.parseFromFilename("file{[a,b]}.txt"));
    }

    @Test
    public void parseFromFilename_readsMultipleBlocks() {
        assertEquals(
                Arrays.asList("a", "b"),
                TagFileManager.parseFromFilename("file{[a]}{[b]}.txt"));
    }

    @Test
    public void parseFromFilename_withoutTagsIsEmpty() {
        assertTrue(TagFileManager.parseFromFilename("file.txt").isEmpty());
    }

    @Test
    public void formatFilenameWithTags_appendsBlock() {
        assertEquals(
                "file{[a,b]}",
                TagFileManager.formatFilenameWithTags("file", Arrays.asList("a", "b")));
    }

    @Test
    public void formatFilenameWithTags_withoutTagsKeepsName() {
        assertEquals(
                "file",
                TagFileManager.formatFilenameWithTags("file", new ArrayList<>()));
    }

    @Test
    public void removeTagsFromFilename_stripsAllBraceBlocks() {
        assertEquals("file.txt", TagFileManager.removeTagsFromFilename("file{[a,b]}.txt"));
        assertEquals("file.txt", TagFileManager.removeTagsFromFilename("file{[a]}{[b]}.txt"));
    }

    // Sidecar 路径

    @Test
    public void buildSidecarPath_usesTagDirectoryBesideFile() {
        assertEquals(
                new FileRef(root_id, "abc/.tag/test.txt.json"),
                TagFileManager.buildSidecarPath(ref("abc/test.txt")));
    }

    @Test
    public void buildSidecarPath_atRootLevel() {
        assertEquals(
                new FileRef(root_id, ".tag/test.txt.json"),
                TagFileManager.buildSidecarPath(ref("test.txt")));
    }

    @Test
    public void buildCleanSidecarPath_stripsFilenameTags() {
        assertEquals(
                new FileRef(root_id, "abc/.tag/test.txt.json"),
                TagFileManager.buildCleanSidecarPath(ref("abc/test{[a]}.txt")));
    }

    @Test
    public void removeFilenameTagsPath_keepsExtension() {
        assertEquals(
                new FileRef(root_id, "abc/test.txt"),
                TagFileManager.removeFilenameTagsPath(ref("abc/test{[a,b]}.txt")));
    }

    // Sidecar 模式

    @Test
    public void addTag_sidecar_createsJsonAndReadsBack() throws Exception {
        createFile("abc/test.txt", "hello");

        TagFileManager manager = new TagFileManager(storage, StoreMode.SIDECAR);
        assertTrue(manager.addTag(ref("abc/test.txt"), "tag1"));

        File sidecar = new File(root, "abc/.tag/test.txt.json");
        assertTrue(sidecar.isFile());

        String json = new String(Files.readAllBytes(sidecar.toPath()), StandardCharsets.UTF_8);
        assertTrue(json.contains("\"tags\""));
        assertTrue(json.contains("tag1"));

        assertEquals(Arrays.asList("tag1"), manager.extractTags(ref("abc/test.txt")));
    }

    @Test
    public void addTag_sidecar_isIdempotent() throws Exception {
        createFile("test.txt", "hello");

        TagFileManager manager = new TagFileManager(storage, StoreMode.SIDECAR);
        assertTrue(manager.addTag(ref("test.txt"), "a"));
        assertTrue(manager.addTag(ref("test.txt"), "a"));

        assertEquals(Arrays.asList("a"), manager.extractTags(ref("test.txt")));
    }

    @Test
    public void removeTag_sidecar_leavesEmptyTags() throws Exception {
        createFile("test.txt", "hello");

        TagFileManager manager = new TagFileManager(storage, StoreMode.SIDECAR);
        manager.addTag(ref("test.txt"), "a");
        manager.addTag(ref("test.txt"), "b");
        assertTrue(manager.removeTag(ref("test.txt"), "a"));

        assertEquals(Arrays.asList("b"), manager.extractTags(ref("test.txt")));
    }

    @Test
    public void removeTag_batch_removesAllListed() throws Exception {
        createFile("test.txt", "hello");

        TagFileManager manager = new TagFileManager(storage, StoreMode.SIDECAR);
        manager.addTag(ref("test.txt"), "a");
        manager.addTag(ref("test.txt"), "b");
        manager.addTag(ref("test.txt"), "c");

        assertTrue(manager.removeTag(ref("test.txt"), Arrays.asList("a", "c")));
        assertEquals(Arrays.asList("b"), manager.extractTags(ref("test.txt")));
    }

    @Test
    public void directory_alwaysUsesSidecar() throws Exception {
        File dir = new File(root, "subdir");
        assertTrue(dir.mkdirs());

        TagFileManager manager = new TagFileManager(storage, StoreMode.FILENAME);
        assertTrue(manager.addTag(ref("subdir"), "folder"));

        // 目录名不允许被改动
        assertTrue(dir.isDirectory());
        assertEquals("subdir", dir.getName());

        // 目录的侧车与其同级 .tag 目录内
        File sidecar = new File(root, ".tag/subdir.json");
        assertTrue(sidecar.isFile());

        assertEquals(Arrays.asList("folder"), manager.extractTags(ref("subdir")));
    }

    // Filename 模式

    @Test
    public void addTag_filename_renamesFile() throws Exception {
        createFile("test.txt", "hello");

        TagFileManager manager = new TagFileManager(storage, StoreMode.FILENAME);
        assertTrue(manager.addTag(ref("test.txt"), "a"));

        assertTrue(new File(root, "test{[a]}.txt").isFile());
        assertFalse(new File(root, "test.txt").exists());

        assertEquals(Arrays.asList("a"), manager.extractTags(ref("test{[a]}.txt")));
    }

    @Test
    public void addTag_filename_keepsExtensionAndAppends() throws Exception {
        createFile("test.txt", "hello");

        TagFileManager manager = new TagFileManager(storage, StoreMode.FILENAME);
        manager.addTag(ref("test.txt"), "a");
        manager.addTag(ref("test{[a]}.txt"), "b");

        assertTrue(new File(root, "test{[a,b]}.txt").isFile());
    }

    @Test
    public void extractTags_withoutMode_fallsBackToOtherMode() throws Exception {
        createFile("test.txt", "hello");

        TagFileManager sidecarManager = new TagFileManager(storage, StoreMode.SIDECAR);
        sidecarManager.addTag(ref("test.txt"), "hidden");

        // 默认模式是 Filename 文件名里没有标签 应该兜底读到 Sidecar
        TagFileManager filenameManager = new TagFileManager(storage, StoreMode.FILENAME);
        assertEquals(Arrays.asList("hidden"), filenameManager.extractTags(ref("test.txt")));
    }

    // 写入后的实际路径

    @Test
    public void addTag_filename_recordsWrittenPath() throws Exception {
        createFile("test.txt", "hello");

        TagFileManager manager = new TagFileManager(storage, StoreMode.FILENAME);
        assertTrue(manager.addTag(ref("test.txt"), "a"));

        // 文件名变了 上层需要知道新路径
        assertEquals(ref("test{[a]}.txt"), manager.getLastWrittenPath());

        // 删掉标签后名字还原 记录跟着回落到原名
        assertTrue(manager.removeTag(ref("test{[a]}.txt"), "a"));
        assertEquals(ref("test.txt"), manager.getLastWrittenPath());
    }

    @Test
    public void addTag_sidecar_recordsUnchangedPath() throws Exception {
        createFile("test.txt", "hello");

        TagFileManager manager = new TagFileManager(storage, StoreMode.SIDECAR);
        assertTrue(manager.addTag(ref("test.txt"), "a"));

        // Sidecar 模式只写侧车 文件本身没动
        assertEquals(ref("test.txt"), manager.getLastWrittenPath());
    }

    @Test
    public void getLastWrittenPath_isNullBeforeAnyWrite() {
        TagFileManager manager = new TagFileManager(storage, StoreMode.SIDECAR);
        assertNull(manager.getLastWrittenPath());
    }

    // 模式转换

    @Test
    public void convertMode_filenameToSidecar_withRemoveOld() throws Exception {
        createFile("test.txt", "hello");

        TagFileManager manager = new TagFileManager(storage, StoreMode.FILENAME);
        manager.addTag(ref("test.txt"), "a");

        assertTrue(manager.convertMode(
                ref("test{[a]}.txt"),
                StoreMode.FILENAME,
                StoreMode.SIDECAR,
                false));

        // 旧文件名恢复 新 Sidecar 生成
        assertTrue(new File(root, "test.txt").isFile());
        assertTrue(new File(root, ".tag/test.txt.json").isFile());
        assertEquals(Arrays.asList("a"), manager.extractTags(ref("test.txt"), StoreMode.SIDECAR));
    }

    @Test
    public void convertMode_sidecarToFilename_withRemoveOld() throws Exception {
        createFile("test.txt", "hello");

        TagFileManager manager = new TagFileManager(storage, StoreMode.SIDECAR);
        manager.addTag(ref("test.txt"), "a");

        assertTrue(manager.convertMode(
                ref("test.txt"),
                StoreMode.SIDECAR,
                StoreMode.FILENAME,
                false));

        assertTrue(new File(root, "test{[a]}.txt").isFile());
        assertFalse(new File(root, ".tag/test.txt.json").exists());
    }

    @Test
    public void convertMode_keepOld_keepsBothSides() throws Exception {
        createFile("test.txt", "hello");

        TagFileManager manager = new TagFileManager(storage, StoreMode.FILENAME);
        manager.addTag(ref("test.txt"), "a");

        assertTrue(manager.convertMode(
                ref("test{[a]}.txt"),
                StoreMode.FILENAME,
                StoreMode.SIDECAR,
                true));

        assertTrue(new File(root, "test{[a]}.txt").isFile());
        assertTrue(new File(root, ".tag/test.txt.json").isFile());
    }

    @Test
    public void removeModeTags_filename_stripsTagBlock() throws Exception {
        createFile("test.txt", "hello");

        TagFileManager manager = new TagFileManager(storage, StoreMode.FILENAME);
        manager.addTag(ref("test.txt"), "a");

        assertTrue(manager.removeModeTags(ref("test{[a]}.txt"), StoreMode.FILENAME));
        assertTrue(new File(root, "test.txt").isFile());
    }

    @Test
    public void removeModeTags_sidecar_deletesSidecar() throws Exception {
        createFile("test.txt", "hello");

        TagFileManager manager = new TagFileManager(storage, StoreMode.SIDECAR);
        manager.addTag(ref("test.txt"), "a");

        assertTrue(manager.removeModeTags(ref("test.txt"), StoreMode.SIDECAR));
        assertFalse(new File(root, ".tag/test.txt.json").exists());
    }

    @Test
    public void extractTags_sidecarFormatsTolerateArrayJson() throws Exception {
        createFile("test.txt", "hello");

        File sidecar = new File(root, ".tag/test.txt.json");
        sidecar.getParentFile().mkdirs();
        Files.write(sidecar.toPath(), "{\"tags\":[\"x\",\"y\"]}".getBytes(StandardCharsets.UTF_8));

        TagFileManager manager = new TagFileManager(storage, StoreMode.SIDECAR);
        assertEquals(Arrays.asList("x", "y"), manager.extractTags(ref("test.txt")));
    }

    @Test
    public void defaultMode_isSidecar() {
        TagFileManager manager = new TagFileManager(storage, null);
        assertEquals(StoreMode.SIDECAR, manager.getDefaultMode());
    }

    @Test
    public void extractTags_missingFileReturnsEmpty() {
        TagFileManager manager = new TagFileManager(storage, StoreMode.SIDECAR);
        assertTrue(manager.extractTags(ref("nope.txt")).isEmpty());
    }

    @Test
    public void addTag_missingFileFails() {
        TagFileManager manager = new TagFileManager(storage, StoreMode.SIDECAR);
        assertFalse(manager.addTag(ref("nope.txt"), "a"));
    }

    @Test
    public void extractTags_ignoresEmptyFilenameTags() {
        List<String> tags = TagFileManager.parseFromFilename("file{[a,,b]}.txt");
        assertEquals(Arrays.asList("a", "b"), tags);
    }
}
