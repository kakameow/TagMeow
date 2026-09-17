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
import java.util.Arrays;
import java.util.List;
import java.util.Map;

// TagLibrary 的 JVM 单元测试
// 使用 org.json（Android 平台内置）所以不需要 Robolectric
public class TagLibraryTest {

    @Rule
    public TemporaryFolder folder = new TemporaryFolder();

    private File config_file;

    @Before
    public void setUp() throws Exception {
        config_file = new File(folder.getRoot(), "config/tag.json");
    }

    private TagLibrary newLibrary() {
        return new TagLibrary(config_file);
    }

    @Test
    public void addTypeAndTag_thenSaveAndReload() {
        TagLibrary library = newLibrary();

        assertTrue(library.addType("work", "#112233"));
        assertTrue(library.addTag("urgent", "work"));
        assertTrue(library.saveTagsToFile());
        assertTrue(config_file.isFile());

        TagLibrary reloaded = newLibrary();
        assertTrue(reloaded.hasType("work"));
        assertTrue(reloaded.hasTag("urgent"));
        assertEquals("#112233", reloaded.getColorByType("work"));
        assertEquals("work", reloaded.getTypeOfTag("urgent"));
    }

    @Test
    public void addType_emptyTypeIsPreserved() {
        TagLibrary library = newLibrary();

        assertTrue(library.addType("empty"));
        assertTrue(library.saveTagsToFile());

        TagLibrary reloaded = newLibrary();
        assertTrue(reloaded.hasType("empty"));
    }

    @Test
    public void addType_invalidColorFallsBackToDefault() {
        TagLibrary library = newLibrary();

        assertTrue(library.addType("bad", "not-a-color"));
        assertEquals(TagLibrary.DEFAULT_COLOR, library.getColorByType("bad"));
    }

    @Test
    public void addTag_tagIsGloballyUnique() {
        TagLibrary library = newLibrary();

        library.addType("a");
        library.addType("b");
        library.addTag("t", "a");

        // 已经存在于 a 时不再重复登记
        assertTrue(library.addTag("t", "b"));
        assertEquals("a", library.getTypeOfTag("t"));
        assertEquals(1, library.getAllTagNames().size());
    }

    @Test
    public void addTag_missingTypeFails() {
        TagLibrary library = newLibrary();

        assertFalse(library.addTag("t", "nope"));
    }

    @Test
    public void removeTag_removesFromOwningType() {
        TagLibrary library = newLibrary();

        library.addType("a");
        library.addTag("t", "a");

        assertTrue(library.removeTag("t"));
        assertFalse(library.hasTag("t"));
        assertFalse(library.removeTag("t"));
    }

    @Test
    public void removeType_removesItsTags() {
        TagLibrary library = newLibrary();

        library.addType("a");
        library.addTag("t", "a");

        assertTrue(library.removeType("a"));
        assertFalse(library.hasType("a"));
        assertFalse(library.hasTag("t"));
    }

    @Test
    public void renameTag_keepsType() {
        TagLibrary library = newLibrary();

        library.addType("a");
        library.addTag("old", "a");

        assertTrue(library.renameTag("old", "new"));
        assertFalse(library.hasTag("old"));
        assertEquals("a", library.getTypeOfTag("new"));
    }

    @Test
    public void renameTag_rejectsExistingName() {
        TagLibrary library = newLibrary();

        library.addType("a");
        library.addTag("one", "a");
        library.addTag("two", "a");

        assertFalse(library.renameTag("one", "two"));
    }

    @Test
    public void renameType_keepsTagsAndColor() {
        TagLibrary library = newLibrary();

        library.addType("old", "#010203");
        library.addTag("t", "old");

        assertTrue(library.renameType("old", "new"));
        assertFalse(library.hasType("old"));
        assertEquals("#010203", library.getColorByType("new"));
        assertEquals("new", library.getTypeOfTag("t"));
    }

    @Test
    public void setTagType_movesTag() {
        TagLibrary library = newLibrary();

        library.addType("a");
        library.addType("b");
        library.addTag("t", "a");

        assertTrue(library.setTagType("t", "b"));
        assertEquals("b", library.getTypeOfTag("t"));
    }

    @Test
    public void setTypeColor_validatesFormat() {
        TagLibrary library = newLibrary();

        library.addType("a", "#000000");

        assertFalse(library.setTypeColor("a", "zzz"));
        assertTrue(library.setTypeColor("a", "#FFFFFF"));
        assertEquals("#FFFFFF", library.getColorByType("a"));
    }

    @Test
    public void isValidHexColor_supportedFormats() {
        assertTrue(TagLibrary.isValidHexColor("#fff"));
        assertTrue(TagLibrary.isValidHexColor("#ffff"));
        assertTrue(TagLibrary.isValidHexColor("#FFC0CB"));
        assertTrue(TagLibrary.isValidHexColor("#FFC0CBAA"));
        assertTrue(TagLibrary.isValidHexColor("FFC0CB"));
        assertFalse(TagLibrary.isValidHexColor(""));
        assertFalse(TagLibrary.isValidHexColor("#"));
        assertFalse(TagLibrary.isValidHexColor("#GGG"));
        assertFalse(TagLibrary.isValidHexColor("#FFFFF"));
    }

    @Test
    public void autoComplete_matchesPrefix() {
        TagLibrary library = newLibrary();

        library.addType("a");
        library.addTag("apple", "a");
        library.addTag("apricot", "a");
        library.addTag("banana", "a");

        List<String> result = library.autoComplete("ap");
        assertEquals(Arrays.asList("apple", "apricot"), result);
        assertTrue(library.autoComplete("").isEmpty());
    }

    @Test
    public void loadTagsFromFile_deduplicatesTags() throws Exception {
        File file = new File(folder.getRoot(), "dup/tag.json");
        file.getParentFile().mkdirs();

        String json = "{\"groups\":["
                + "{\"type\":\"a\",\"color\":\"#111111\",\"tags\":[\"t\",\"t\",\"\"]},"
                + "{\"type\":\"b\",\"color\":\"#222222\",\"tags\":[\"t\"]}"
                + "]}";

        Files.write(file.toPath(), json.getBytes(StandardCharsets.UTF_8));

        TagLibrary library = newLibrary();
        assertTrue(library.loadTagsFromFile(file));

        // Tag 全局唯一：只保留第一次出现的位置
        assertEquals(Arrays.asList("t"), library.getAllTagNames());
        assertEquals("a", library.getTypeOfTag("t"));
    }

    @Test
    public void loadTagsFromFile_invalidJsonReturnsFalse() throws Exception {
        File file = new File(folder.getRoot(), "bad/tag.json");
        file.getParentFile().mkdirs();
        Files.write(file.toPath(), "not json".getBytes(StandardCharsets.UTF_8));

        TagLibrary library = newLibrary();
        assertFalse(library.loadTagsFromFile(file));
        assertFalse(library.getLastError().isEmpty());
    }

    @Test
    public void getTypeTag_returnsIndependentCopy() {
        TagLibrary library = newLibrary();

        library.addType("a");
        library.addTag("t", "a");

        Map<String, List<String>> view = library.getTypeTag();
        assertEquals(Arrays.asList("t"), view.get("a"));

        // 外部修改不应该影响内部状态
        try {
            view.get("a").add("hacked");
        } catch (UnsupportedOperationException expected) {
            // 只读视图同样可以接受
        }

        assertEquals(1, library.getAllTagNames().size());
    }

    @Test
    public void getColorByType_missingTypeReturnsEmpty() {
        TagLibrary library = newLibrary();

        assertEquals("", library.getColorByType("nope"));
    }

    @Test
    public void getTypeOfTag_missingTagReturnsEmpty() {
        TagLibrary library = newLibrary();

        assertEquals("", library.getTypeOfTag("nope"));
    }

    @Test
    public void constructor_createsMissingConfigFile() {
        assertFalse(config_file.exists());

        TagLibrary library = newLibrary();

        assertTrue(config_file.exists());
        assertFalse(library.getLastError().isEmpty());
    }

    @Test
    public void saveTagsToFile_writesCppCompatibleFormat() throws Exception {
        TagLibrary library = newLibrary();

        library.addType("work", "#112233");
        library.addTag("urgent", "work");
        assertTrue(library.saveTagsToFile());

        String json = new String(Files.readAllBytes(config_file.toPath()), StandardCharsets.UTF_8);
        assertTrue(json.contains("\"groups\""));
        assertTrue(json.contains("\"type\""));
        assertTrue(json.contains("\"color\""));
        assertTrue(json.contains("\"tags\""));
    }

    @Test
    public void clearInvalidTag_reportsChange() {
        TagLibrary library = newLibrary();

        library.addType("a");
        library.addTag("t", "a");

        assertFalse(library.clearInvalidTag());
    }

    @Test
    public void getConfigFile_returnsConstructorFile() {
        TagLibrary library = newLibrary();

        assertEquals(config_file, library.getConfigFile());
        assertNull(library.getTypeColor().get("nope"));
    }

    // ---------------- mergeTags（导入标签库用） ----------------

    private File writeJson(String name, String json) throws Exception {
        File file = new File(folder.getRoot(), name);
        Files.write(file.toPath(), json.getBytes(StandardCharsets.UTF_8));

        return file;
    }

    @Test
    public void mergeTags_replacesEverythingWhenEmpty() throws Exception {
        TagLibrary library = newLibrary();
        File incoming = writeJson("in.json",
                "{\"groups\":[{\"type\":\"work\",\"color\":\"#112233\",\"tags\":[\"urgent\"]}]}");

        assertTrue(library.mergeTags(incoming));
        assertEquals(Arrays.asList("work"), library.getAllTypeNames());
        assertEquals("#112233", library.getColorByType("work"));
        assertTrue(library.hasTag("urgent"));
    }

    @Test
    public void mergeTags_addsOnlyMissingTypesAndTags() throws Exception {
        TagLibrary library = newLibrary();

        library.addType("work", "#112233");
        library.addTag("urgent", "work");

        File incoming = writeJson("in.json",
                "{\"groups\":["
                        + "{\"type\":\"work\",\"color\":\"#999999\",\"tags\":[\"urgent\",\"todo\"]},"
                        + "{\"type\":\"study\",\"color\":\"#445566\",\"tags\":[\"math\"]}]}");

        assertTrue(library.mergeTags(incoming));

        // 新类型整组并进来
        assertEquals(Arrays.asList("work", "study"), library.getAllTypeNames());
        assertEquals("#445566", library.getColorByType("study"));

        // 已有类型保留自己的颜色，只补它没有的 tag
        assertEquals("#112233", library.getColorByType("work"));
        assertEquals(Arrays.asList("urgent", "todo"), library.getTypeTag().get("work"));
        assertEquals(Arrays.asList("math"), library.getTypeTag().get("study"));
    }

    @Test
    public void mergeTags_keepsTagsGloballyUnique() throws Exception {
        TagLibrary library = newLibrary();

        library.addType("work");
        library.addTag("shared", "work");

        File incoming = writeJson("in.json",
                "{\"groups\":[{\"type\":\"study\",\"color\":\"#445566\",\"tags\":[\"shared\",\"math\"]}]}");

        assertTrue(library.mergeTags(incoming));

        // shared 已经全局存在，不会在 study 下再来一份
        assertEquals(Arrays.asList("math"), library.getTypeTag().get("study"));
        assertEquals("work", library.getTypeOfTag("shared"));
    }

    @Test
    public void mergeTags_neverDropsTheExistingData() throws Exception {
        TagLibrary library = newLibrary();

        library.addType("keep", "#010203");
        library.addTag("mine", "keep");

        File incoming = writeJson("in.json",
                "{\"groups\":[{\"type\":\"other\",\"color\":\"#445566\",\"tags\":[]}]}");

        assertTrue(library.mergeTags(incoming));

        // 本类自己的类型 / 标签一个都没少
        assertTrue(library.hasType("keep"));
        assertTrue(library.hasTag("mine"));
        assertEquals("#010203", library.getColorByType("keep"));

        // 空的 incoming 类型也照样并进来
        assertTrue(library.hasType("other"));
        assertEquals(Arrays.asList("keep", "other"), library.getAllTypeNames());
    }

    @Test
    public void mergeTags_reportsMissingFileAndBadJson() throws Exception {
        TagLibrary library = newLibrary();

        assertFalse(library.mergeTags(new File(folder.getRoot(), "nope.json")));

        File broken = writeJson("broken.json", "{ not json");
        assertFalse(library.mergeTags(broken));
    }
}
