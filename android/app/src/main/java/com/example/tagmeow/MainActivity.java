package com.example.tagmeow;

import android.Manifest;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.UriPermission;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.text.InputType;
import android.text.TextUtils;
import android.util.Log;
import android.util.LruCache;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.MimeTypeMap;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Space;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.EdgeToEdge;
import androidx.activity.OnBackPressedCallback;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;
import androidx.core.content.FileProvider;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;

import com.google.android.material.card.MaterialCardView;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.Date;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

// TagMeow 主界面

// 按 TagMeow_Android_Layout_Mockup.html 实现：
// - 顶部：文件数与标签数统计
// - 浏览：标签筛选（包含 / 排除 / 仅匹配）+ 文件列表
// - 目录：受管理目录的增删与索引刷新
// - 标签：标签库（组 -> 标签 带颜色）
// - 设置：设置
// - 同步：占位

// 线程约定：
// - 所有模块调用都在 worker 单线程上执行
// - 只有主线程才读写 View

public class MainActivity extends AppCompatActivity {

    // logcat 里按这个 tag 过滤：adb logcat -s TagMeow
    private static final String TAG = "TagMeow";

    // 项目主页（设置里的「关于 TagMeow」用系统浏览器打开它）
    private static final String PROJECT_URL = "https://github.com/kakameow/TagMeow";

    private static final String PREFS_NAME = "tagmeow";
    private static final String KEY_DEFAULT_MODE = "default_mode";
    private static final String KEY_LANGUAGE = "language";

    // 第一次运行时的默认语言（界面上不再有「跟随系统」这一项）
    private static final String DEFAULT_LANGUAGE_CODE = "zh_CN";
    // 存储方式只剩一种：所有文件访问（绝对路径）+ 应用内选目录
    // 系统选择器那条路整个砍掉了（测试机上被 ROM 卡死 见 git 03191d2）

    // 标签颜色的默认值（新类型默认用这个）
    private static final String DEFAULT_TAG_COLOR = "#FFB6C1";

    // 编辑器两种表单
    private static final int EDITOR_MODE_TYPE = 0;
    private static final int EDITOR_MODE_TAG = 1;

    // 导出用的默认文件名（每次走系统「保存到…」对话框）
    private static final String EXPORT_FILE_NAME = "TagMeow-tags.json";

    // 不要给系统选择器种子「内部存储根」
    // （content://com.android.externalstorage.documents/document/primary%3A）
    // Android 11+ 不允许把存储卷根目录授权给应用：
    // 选择器会进到不可用视图 —— 面包屑不可见、列表「无任何文件」、
    // 按钮写着「无法使用此文件夹 / 为保护您的隐私，请选择其他文件夹」且不可点，
    // 用户既选不了也进不去子目录（vivo V2425A 实测）
    // 另外：带一个「本应用没有授权的 Uri」当种子也会进到同样的不可用视图
    // 所以现在只拿「手里真有授权、且现在读得到」的目录当种子，
    // 一个都没有时不传初始位置，让系统开它自己的默认视图

    // 内容展示的颜色固定：列表、卡片、文字、边框都不跟主题走
    private static final int COLOR_TEXT = 0xFF20242B;
    private static final int COLOR_MUTED = 0xFF8C939F;
    private static final int COLOR_BORDER = 0xFFE0E3E7;
    private static final int COLOR_ACTIVE_BG = 0xFFF0F2F5;

    // 类型没设颜色时的兜底色（标签自己的颜色也不属于界面主题）
    private static final int COLOR_PINK = 0xFFFFC0CB;

    // 强调色
    private static final int COLOR_ACCENT = 0xFFFFB6C1;

    // 底部导航的文案键（管理并入了设置）
    private static final String[] TAB_LABEL_KEYS = {
            "nav.browse", "nav.directory", "nav.tags", "nav.sync", "nav.settings"};

    // 底部导航图标：res/drawable 里的矢量图（Lucide 的 svg 转换而来）
    private static final int[] TAB_ICONS = {
            R.drawable.notepad_text, R.drawable.folder, R.drawable.tag,
            R.drawable.arrow_right_left, R.drawable.settings};

    private static final String FALLBACK_COLOR = "#FFC0CB";

    // 缩略图取多大（像素）
    private static final int THUMB_SIZE = 96;

    // 一次列表最多取多少张缩略图（超出的显示类型图标 防止大文件夹把内存吃光）
    private static final int THUMB_LIMIT = 300;

    // 缩略图缓存上限（KB）：reload() 会重画整个列表 缓存住就不用反复解码
    private static final int THUMB_CACHE_KB = 8 * 1024;

    private final ExecutorService worker = Executors.newSingleThreadExecutor();

    // 缩略图单独一个线程池：图片多的时候不要把 worker 队列堵住
    private final ExecutorService thumb_worker = Executors.newFixedThreadPool(2);

    // 缩略图缓存（key 里带上修改时间 文件改过就会重新取）
    private final LruCache<String, Bitmap> thumb_cache =
            new LruCache<String, Bitmap>(THUMB_CACHE_KB) {
                @Override
                protected int sizeOf(String key, Bitmap bitmap) {
                    return bitmap.getByteCount() / 1024;
                }
            };

    private SharedPreferences prefs;

    // 存储实现：固定「所有文件访问」（绝对路径）
    private StorageAccess storage;

    // 从系统设置页回来时要检查「所有文件访问」权限
    private boolean pending_all_files_request = false;


    private TagServe serve;

    private LinearLayout general_group;

    private LinearLayout data_group;

    private LinearLayout support_group;

    // 当前语言代码（zh_CN / en_US）界面上没有「跟随系统」这一项
    private String language_code = DEFAULT_LANGUAGE_CODE;

    private String status_text = "";

    // 每个受管理目录能不能真的读（在 worker 上算好 UI 线程不去查 provider）
    private final Map<String, Boolean> root_access = new LinkedHashMap<>();

    private ActivityResultLauncher<String> export_picker;

    private ActivityResultLauncher<String[]> import_picker;

    // API < 30 用运行时读写权限代替「所有文件访问」
    private ActivityResultLauncher<String[]> all_files_permission_picker;

    private View[] tab_views;

    private LinearLayout[] nav_items;

    private ImageView[] nav_icons;

    private TextView[] nav_labels;

    private TextView tv_stat_files;

    private TextView tv_stat_tags;

    private TextView tv_files_title;

    private TextView tv_files_count;

    private TextView tv_files_empty;

    private TextView btn_up;

    private LinearLayout files_list;

    private LinearLayout dir_list;

    // 目录管理子界面
    private View dir_manage_overlay;

    private LinearLayout dir_manage_list;

    private TextView tv_dir_manage_empty;

    // 目录管理里被选中的 root_id（多选）
    private final Set<String> selected_root_ids = new LinkedHashSet<>();

    // 目录浏览子界面（所有文件访问模式下的「添加目录」）
    private View dir_browser_overlay;

    private HorizontalScrollView dir_browser_crumbs;

    private LinearLayout dir_browser_crumb_row;

    private LinearLayout dir_browser_list;

    private View dir_browser_empty;

    private File dir_browser_path;

    private TextView tv_dir_empty;

    private LinearLayout type_list;

    private TextView tv_type_empty;

    private TextView edit_include;

    private TextView edit_exclude;

    private TextView edit_only;

    // 搜索条件通过标签库选择界面填充
    private final Set<String> include_tags = new LinkedHashSet<>();

    private final Set<String> exclude_tags = new LinkedHashSet<>();

    private final Set<String> only_tags = new LinkedHashSet<>();

    private FrameLayout overlay_host;

    // 子界面一：给文件加标签
    private View file_tag_overlay;

    private LinearLayout file_tag_current;

    private LinearLayout file_tag_library;

    private FileDatabase.FileInfo file_tag_target;

    private final Set<String> file_tag_working = new LinkedHashSet<>();

    // 子界面二：搜索筛选
    private View search_tag_overlay;

    private LinearLayout tags_include;

    private LinearLayout tags_exclude;

    private LinearLayout tags_only;

    private TextView title_include;

    private TextView title_exclude;

    private TextView title_only;

    private TextView tv_current_group;

    private LinearLayout search_tag_library;

    private MaterialCardView group_include;

    private MaterialCardView group_exclude;

    private MaterialCardView group_only;

    // 子界面三：类型 / 标签编辑
    private View tag_editor_overlay;

    private ScrollView editor_scroll;

    private TextView seg_type;

    private TextView seg_tag;

    private TextView tv_editor_title;

    private LinearLayout form_type;

    private LinearLayout form_tag;

    private EditText edit_type_name;

    // 改类型名用：填「新类型名称」
    private EditText edit_type_new_name;

    private EditText edit_type_color;

    private View color_swatch;

    private TextView btn_pick_color;

    private LinearLayout type_color_preview;

    private LinearLayout type_list_display;

    private LinearLayout btn_select_type;

    private View sel_type_dot;

    private TextView sel_type_name;

    private LinearLayout type_option_list;

    private EditText edit_tag_name;

    private LinearLayout tag_list_display;

    private int editor_mode = 0;

    // 从列表回填选中的目标（null 表示新建）
    private String editor_selected_type;

    private String editor_selected_tag;

    // 标签库的标签会加进「当前容器」 默认第一个（包含）
    private int active_filter_group = 0;

    private FileRef current_directory;

    private boolean search_mode = false;

    private String search_summary = "";

    private final List<FileDatabase.FileInfo> current_files = new ArrayList<>();

    private final List<DirectoryConfigManager.Directory> current_roots = new ArrayList<>();

    private Map<String, List<String>> tag_types = new LinkedHashMap<>();

    private Map<String, String> tag_colors = new LinkedHashMap<>();

    private long indexed_file_count = 0;

    @Override
    protected void onCreate(Bundle saved_instance_state) {
        super.onCreate(saved_instance_state);
        EdgeToEdge.enable(this);
        setContentView(R.layout.activity_main);
        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.main), (v, insets) -> {
            Insets systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars());
            v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom);
            return insets;
        });

        prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);

        // 载入语言字典：老版本可能存过 "system"（跟随系统） Lang 会把它解析成具体语言
        Lang.init(getApplicationContext(), prefs.getString(KEY_LANGUAGE, DEFAULT_LANGUAGE_CODE));

        // 界面只认具体语言代码
        language_code = Lang.current();

        bindViews();
        buildNav();

        // 必须在 buildNav 之后：selectTab 会读写导航项
        selectTab(0);

        // 返回（含手势返回）：子界面打开时先关子界面
        getOnBackPressedDispatcher().addCallback(this, new OnBackPressedCallback(true) {
            @Override
            public void handleOnBackPressed() {
                if (overlay_host != null && overlay_host.getVisibility() == View.VISIBLE) {
                    hideOverlay();
                    return;
                }

                // 没有子界面时交回系统处理
                setEnabled(false);
                getOnBackPressedDispatcher().onBackPressed();
                setEnabled(true);
            }
        });

        // 导出：挑一个保存位置
        export_picker = registerForActivityResult(
                new ActivityResultContracts.CreateDocument("application/json"),
                this::onExportPicked);

        // 导入：挑一个 JSON 文件
        import_picker = registerForActivityResult(
                new ActivityResultContracts.OpenDocument(),
                this::onImportPicked);

        // API < 30：用运行时读写权限代替「所有文件访问」
        all_files_permission_picker = registerForActivityResult(
                new ActivityResultContracts.RequestMultiplePermissions(),
                result -> {
                    if (hasAllFilesAccess()) {
                        restartEngine();
                    } else {
                        toast(Lang.get("storage.perm_denied"));
                    }
                });

        initEngine();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        worker.execute(() -> {
            if (serve != null) {
                serve.getFileDatabase().close();
            }
        });

        worker.shutdown();
        thumb_worker.shutdown();
    }

    private void bindViews() {
        tv_stat_files = findViewById(R.id.tvStatFiles);
        tv_stat_tags = findViewById(R.id.tvStatTags);
        tv_files_title = findViewById(R.id.tvFilesTitle);
        tv_files_count = findViewById(R.id.tvFilesCount);
        tv_files_empty = findViewById(R.id.tvFilesEmpty);
        btn_up = findViewById(R.id.btnUp);
        files_list = findViewById(R.id.filesList);
        dir_list = findViewById(R.id.dirList);
        tv_dir_empty = findViewById(R.id.tvDirEmpty);
        type_list = findViewById(R.id.typeList);
        tv_type_empty = findViewById(R.id.tvTypeEmpty);
        general_group = findViewById(R.id.groupGeneral);
        data_group = findViewById(R.id.groupData);
        support_group = findViewById(R.id.groupSupport);

        edit_include = findViewById(R.id.editInclude);
        edit_exclude = findViewById(R.id.editExclude);
        edit_only = findViewById(R.id.editOnly);

        overlay_host = findViewById(R.id.overlayHost);

        // 点搜索容器 -> 打开筛选标签子界面（默认选中对应的容器）
        edit_include.setOnClickListener(v -> showSearchTagOverlay(0));
        edit_exclude.setOnClickListener(v -> showSearchTagOverlay(1));
        edit_only.setOnClickListener(v -> showSearchTagOverlay(2));

        renderSearchChips();
        applyStaticTexts();

        tab_views = new View[]{
                findViewById(R.id.viewBrowse),
                findViewById(R.id.viewDirectory),
                findViewById(R.id.viewTags),
                findViewById(R.id.viewSync),
                findViewById(R.id.viewSettings),
        };

        findViewById(R.id.btnSearch).setOnClickListener(v -> doSearch());
        findViewById(R.id.btnClearSearch).setOnClickListener(v -> clearSearch());
        findViewById(R.id.btnRefresh).setOnClickListener(v -> runAction(Lang.get("dir.action_refresh"), () -> {
            boolean ok = serve.refreshAll();
            toast(ok ? Lang.f("dir.refresh_done_count", serve.getFileDatabase().countFiles())
                    : Lang.get("common.refresh_failed") + serve.getLastError());
        }));

        btn_up.setOnClickListener(v -> openParentDirectory());

        findViewById(R.id.btnAddRoot).setOnClickListener(v -> pickRoot());
        findViewById(R.id.btnManageRoots).setOnClickListener(v -> showDirManageOverlay());

        findViewById(R.id.btnNewType).setOnClickListener(v -> showTypeEditor(null));
        findViewById(R.id.btnNewTag).setOnClickListener(v -> showTagEditor(null));

        findViewById(R.id.btnSaveSettings).setOnClickListener(v -> runAction(Lang.get("settings.save"), () -> {
            boolean ok = serve.saveTag();
            toast(ok ? Lang.get("settings.saved") : Lang.get("common.save_failed") + serve.getLastError());
        }));
    }

    private void buildNav() {
        LinearLayout nav = findViewById(R.id.bottomNav);

        nav_items = new LinearLayout[TAB_LABEL_KEYS.length];
        nav_icons = new ImageView[TAB_LABEL_KEYS.length];
        nav_labels = new TextView[TAB_LABEL_KEYS.length];

        for (int i = 0; i < TAB_LABEL_KEYS.length; i++) {
            View item = getLayoutInflater().inflate(R.layout.item_nav, nav, false);

            nav_items[i] = (LinearLayout) item;
            nav_icons[i] = item.findViewById(R.id.navIcon);
            nav_labels[i] = item.findViewById(R.id.navLabel);

            nav_icons[i].setImageResource(TAB_ICONS[i]);
            nav_labels[i].setText(Lang.get(TAB_LABEL_KEYS[i]));

            // 与设计稿一致：标签入口用强调色（跟随主题）
            if (i == 2) {
                setIconTint(nav_icons[i], COLOR_ACCENT);
                nav_labels[i].setTextColor(COLOR_ACCENT);
            }

            final int index = i;
            item.setOnClickListener(v -> selectTab(index));

            nav.addView(item);
        }
    }

    private void selectTab(int index) {
        for (int i = 0; i < tab_views.length; i++) {
            tab_views[i].setVisibility(i == index ? View.VISIBLE : View.GONE);

            GradientDrawable background = new GradientDrawable();
            background.setCornerRadius(dp(11));
            background.setColor(i == index ? COLOR_ACTIVE_BG : Color.TRANSPARENT);
            nav_items[i].setBackground(background);

            int color = i == index ? COLOR_TEXT : (i == 2 ? COLOR_ACCENT : 0xFF626A75);
            setIconTint(nav_icons[i], color);
            nav_labels[i].setTextColor(color);
        }

        reload();
    }

    private void initEngine() {
        worker.execute(() -> {
            try {
                // logcat 诊断：本应用当前持有的持久化授权
                // 授权被系统/厂商回收时这里会少掉对应的目录
                logPersistedGrants();
                if (serve != null) {
                    serve.getFileDatabase().close();
                    serve = null;
                }

                File configDir = new File(getFilesDir(), "config");
                if (!configDir.exists() && !configDir.mkdirs()) {
                    toast(Lang.get("run.config_dir_failed"));
                    return;
                }

                // 只有一种存储方式：所有文件访问（绝对路径）
                // 目录配置沿用「所有文件访问」那份 path-files.json
                File dirConfigFile = new File(configDir, "path-files.json");

                storage = new StorageAccess();

                serve = new TagServe(
                        getApplicationContext(),
                        dirConfigFile,
                        new File(configDir, "tag.json"),
                        new File(configDir, "index.db"),
                        storage,
                        readDefaultMode());

                // index.db 是两种存储方式共用的：先把不属于当前配置的残留行清掉
                // （另一种模式的行、已经删掉的目录的行）
                // 否则首页「文件 N」是这些的合计 搜索也会串模式
                serve.purgeForeignRoots();

                // 清完之后当前模式一条记录都没有（比如刚从另一种模式切回来）就顺手重扫一次
                // 用的是 refreshAll 不是 reLoadRoot：后者会把读不到的目录直接从配置里丢掉
                if (serve.getFileDatabase().countFiles() == 0
                        && !serve.getDirectoryConfigManager().getDirList().isEmpty()) {
                    serve.refreshAll();
                }

                reload();
            } catch (Throwable error) {
                toast(Lang.get("common.init_failed") + error);
            }
        });
    }

    // 在 worker 上取快照 然后回主线程重画
    private void reload() {
        worker.execute(() -> {
            TagServe current = serve;

            if (current == null) {
                return;
            }

            // 展示用：有效的和「授权丢了」的都要列出来
            // 否则目录从列表里凭空消失 用户既看不到也没法重新授权
            final List<DirectoryConfigManager.Directory> roots =
                    current.getDirectoryConfigManager().getDirList();
            final Map<String, Boolean> access = new LinkedHashMap<>();

            // 真正读一次：卸载重装 / 被系统回收之后持久化授权会没掉
            for (DirectoryConfigManager.Directory root : roots) {
                access.put(root.getId(), hasAccess(root));
            }
            final Map<String, List<String>> types = current.getTypeTag();
            final Map<String, String> colors = current.getTypeColor();
            final long file_count = current.getFileDatabase().countFiles();
            final int tagCount = current.getAllTagNames().size();

            FileRef directory = current_directory;

            if (directory == null) {
                // 默认落在第一个「真能读」的目录上
                for (DirectoryConfigManager.Directory root : roots) {
                    if (Boolean.TRUE.equals(access.get(root.getId()))) {
                        directory = new FileRef(root.getId(), "");
                        break;
                    }
                }

                if (directory == null && !roots.isEmpty()) {
                    directory = new FileRef(roots.get(0).getId(), "");
                }
            }

            final FileRef target = directory;
            final List<FileDatabase.FileInfo> entries = (target == null || search_mode)
                    ? new ArrayList<>()
                    : current.listDirectory(target);

            final String info = buildStatusText(current, file_count, roots.size());

            runOnUiThread(() -> {
                current_roots.clear();
                current_roots.addAll(roots);
                root_access.clear();
                root_access.putAll(access);
                tag_types = types;
                tag_colors = colors;
                indexed_file_count = file_count;

                tv_stat_files.setText(Lang.get("stat.files_prefix") + file_count);
                tv_stat_tags.setText(Lang.get("stat.tags_prefix") + tagCount);
                status_text = info;
                renderSettings();

                if (!search_mode) {
                    current_directory = target;
                    current_files.clear();
                    current_files.addAll(entries);
                }

                renderDirectories();
                renderTagLibrary();
                renderFiles(search_mode ? current_files : entries);

                // 编辑器子界面开着的时候 也要跟着最新标签库刷新
                // 否则刚添加 / 删除的类型不会出现在列表里
                if (tag_editor_overlay != null) {
                    renderEditorTypeList();
                    renderEditorTypeOptions();
                    renderEditorTagList();
                }

                // 目录管理子界面开着的时候也要跟着刷新（有效/无效、增删后）
                if (dir_manage_overlay != null) {
                    renderDirManageList();
                }

            });
        });
    }

    // 设置页顶部的状态摘要
    private String buildStatusText(TagServe current, long file_count, int root_count) {

        return Lang.f("settings.status_line", root_count, file_count, name(current.getDefaultMode()));
    }

    // 渲染：文件列表

    private void renderFiles(List<FileDatabase.FileInfo> files) {
        files_list.removeAllViews();

        if (search_mode) {
            tv_files_title.setText(Lang.get("browse.search_results"));
            tv_files_count.setText(files.size() + Lang.get("common.count_suffix"));
            btn_up.setVisibility(View.GONE);
        } else {
            tv_files_title.setText(current_directory == null || current_directory.isRoot()
                    ? Lang.get("browse.files")
                    : current_directory.getName());
            tv_files_count.setText(files.size() + Lang.get("common.count_suffix"));
            btn_up.setVisibility(current_directory != null && !current_directory.isRoot()
                    ? View.VISIBLE
                    : View.GONE);
        }

        boolean empty = files.isEmpty();

        if (empty) {
            if (search_mode) {
                tv_files_empty.setText(Lang.get("browse.empty_search_prefix") + search_summary + Lang.get("browse.empty_search_suffix"));
            } else if (current_roots.isEmpty()) {
                tv_files_empty.setText(Lang.get("browse.empty_no_root"));
            } else {
                tv_files_empty.setText(Lang.get("browse.empty_dir"));
            }
        }

        tv_files_empty.setVisibility(empty ? View.VISIBLE : View.GONE);

        int thumbBudget = THUMB_LIMIT;

        for (FileDatabase.FileInfo info : files) {
            View row = getLayoutInflater().inflate(R.layout.item_file, files_list, false);

            ImageView icon = row.findViewById(R.id.fileIcon);
            TextView name = row.findViewById(R.id.fileName);
            TextView path = row.findViewById(R.id.filePath);
            LinearLayout tags = row.findViewById(R.id.fileTags);
            TextView moreLabel = row.findViewById(R.id.fileMoreLabel);

            moreLabel.setText(Lang.get("tag.row_label"));

            bindFileIcon(icon, info, dp(7));

            if (wantsThumbnail(info, thumbBudget)) {
                thumbBudget--;
                loadThumbnail(icon, info);
            }

            name.setText(info.file_ref.isRoot() ? "(root)" : info.file_ref.getName());

            String parent = info.file_ref.getParentPath();
            path.setText(info.file_ref.getRootId() + (parent.isEmpty() ? "" : " / " + parent) + (info.is_directory ? Lang.get("browse.dir_suffix") : "  ·  " + formatSize(info.file_size)));

            List<String> infoTags = info.tags == null ? new ArrayList<>() : info.tags;

            int shown = 0;
            for (String tag : infoTags) {
                if (shown++ >= 4) {
                    tags.addView(buildPlainChip("＋" + (infoTags.size() - 4)));
                    break;
                }

                tags.addView(buildTagChip(tag, colorOfTag(tag)));
            }

            row.setOnClickListener(v -> onFileClicked(info));

            // 右侧 2/10 区域：打开这个文件的标签修改界面
            row.findViewById(R.id.fileMore).setOnClickListener(v -> showFileTagOverlay(info));

            files_list.addView(row);
        }
    }

    // 先按类型画矢量图 图片 / 视频再由调用方决定要不要去取缩略图
    private void bindFileIcon(ImageView icon, FileDatabase.FileInfo info, int padding) {
        icon.setImageResource(iconResOf(info));
        icon.setImageTintList(ColorStateList.valueOf(0xFF59616E));
        icon.setPadding(padding, padding, padding, padding);
        icon.setScaleType(ImageView.ScaleType.FIT_CENTER);
        icon.setBackground(rounded(COLOR_ACTIVE_BG, 9, 0, 0));
    }

    // 能取缩略图、而且还在这次列表的预算之内
    private boolean wantsThumbnail(FileDatabase.FileInfo info, int budget) {
        return budget > 0 && !info.is_directory && FileTypes.hasThumbnail(info.file_ref.getName());
    }

    // 缩略图在后台取：失败（格式不支持 / 太大 / 没权限）就保持矢量图不动
    private void loadThumbnail(final ImageView icon, final FileDatabase.FileInfo info) {
        final String key = info.file_ref.getRootId() + ":" + info.file_ref.getRelativePath()
                + ":" + info.file_mtime;

        Bitmap cached = thumb_cache.get(key);

        if (cached != null) {
            showThumbnail(icon, cached);
            return;
        }

        thumb_worker.execute(() -> {
            StorageAccess current = storage;

            if (current == null) {
                return;
            }

            String locator = current.locatorOf(info.file_ref);

            if (locator == null || locator.isEmpty()) {
                return;
            }

            Bitmap bitmap = loadThumbnailBitmap(locator);

            if (bitmap == null) {
                return;
            }

            thumb_cache.put(key, bitmap);
            runOnUiThread(() -> showThumbnail(icon, bitmap));
        });
    }

    // 缩略图只有一个来源了：绝对路径（所有文件访问模式）
    // 原来 SAF 的 ContentResolver.loadThumbnail 分支跟着系统选择器一起砍了
    private Bitmap loadThumbnailBitmap(String locator) {
        return decodeScaledThumbnail(locator);
    }

    // 自己缩放解码（开两次：先量尺寸、再按 inSampleSize 解）
    // 注意：BitmapFactory 解不了视频 视频缩略图会退回类型图标
    private static Bitmap decodeScaledThumbnail(String path) {
        BitmapFactory.Options bounds = new BitmapFactory.Options();
        bounds.inJustDecodeBounds = true;
        BitmapFactory.decodeFile(path, bounds);

        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) {
            return null;
        }

        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inSampleSize = sampleSizeFor(bounds.outWidth, bounds.outHeight, THUMB_SIZE);

        try {
            return BitmapFactory.decodeFile(path, options);
        } catch (RuntimeException error) {
            return null;
        }
    }

    private static int sampleSizeFor(int width, int height, int target) {
        int sample = 1;

        while (width / (sample * 2) >= target && height / (sample * 2) >= target) {
            sample *= 2;
        }

        return sample;
    }

    // 取到缩略图：去掉 tint 铺满整格
    private void showThumbnail(ImageView icon, Bitmap bitmap) {
        icon.setImageTintList(null);
        icon.setPadding(0, 0, 0, 0);
        icon.setScaleType(ImageView.ScaleType.CENTER_CROP);
        icon.setImageBitmap(bitmap);
    }

    // 类型 -> 矢量图（res/drawable 里由 svg 转来的那几张）
    private int iconResOf(FileDatabase.FileInfo info) {
        if (info.is_directory) {
            return R.drawable.folder;
        }

        String name = info.file_ref.getName();

        if (FileTypes.isImage(name)) {
            return R.drawable.image;
        }

        if (FileTypes.isVideo(name)) {
            return R.drawable.square_play;
        }

        if (FileTypes.isAudio(name)) {
            return R.drawable.audio_lines;
        }

        if (FileTypes.isText(name)) {
            return R.drawable.file_text;
        }

        return R.drawable.file_question_mark;
    }

    private void onFileClicked(FileDatabase.FileInfo info) {
        if (info.is_directory) {
            current_directory = info.file_ref;
            search_mode = false;
            reload();
            return;
        }

        // 单击文件：交给系统推荐的应用打开
        openFileWithSystem(info);
    }

    private void openParentDirectory() {
        if (current_directory == null || current_directory.isRoot()) {
            return;
        }

        current_directory = current_directory.getParent();
        search_mode = false;
        reload();
    }

    private void renderDirectories() {
        dir_list.removeAllViews();

        tv_dir_empty.setVisibility(current_roots.isEmpty() ? View.VISIBLE : View.GONE);

        for (DirectoryConfigManager.Directory directory : current_roots) {
            View row = getLayoutInflater().inflate(R.layout.item_dir, dir_list, false);

            // 读不到的目录标出来（卸载重装 / 被系统回收之后授权就没了）
            boolean granted = Boolean.TRUE.equals(root_access.get(directory.getId()));

            TextView path = row.findViewById(R.id.dirPath);
            path.setText(directory.getDisplayName() + "　·　" + directory.getId()
                    + (granted ? "" : Lang.get("dir.permission_lost")));

            if (current_directory != null && directory.getId().equals(current_directory.getRootId())) {
                row.setBackground(rounded(COLOR_ACTIVE_BG, 10, 0, 0));
            }

            row.setOnClickListener(v -> {
                if (!Boolean.TRUE.equals(root_access.get(directory.getId()))) {
                    // 读不到也照样打开（不能再把点击拦死） 只提示一句
                    toast(Lang.get("dir.permission_lost_hint"));
                }

                current_directory = new FileRef(directory.getId(), "");
                search_mode = false;
                selectTab(0);
            });

            // 目录右边的三个点：打开同一个「目录管理」子界面 并选中这一条
            row.findViewById(R.id.dirMore).setOnClickListener(v -> showDirManageOverlay(directory.getId()));

            dir_list.addView(row);
        }
    }

    private void renderTagLibrary() {
        type_list.removeAllViews();

        tv_type_empty.setVisibility(tag_types.isEmpty() ? View.VISIBLE : View.GONE);

        for (Map.Entry<String, List<String>> entry : tag_types.entrySet()) {
            final String type = entry.getKey();
            final List<String> tags = entry.getValue();
            final int color = colorOfType(type);

            View card = getLayoutInflater().inflate(R.layout.item_type, type_list, false);

            View dot = card.findViewById(R.id.typeDot);
            TextView name = card.findViewById(R.id.typeName);
            TextView count = card.findViewById(R.id.typeCount);
            LinearLayout tagRow = card.findViewById(R.id.typeTags);
            View head = card.findViewById(R.id.typeHead);

            dot.setBackground(rounded(color, 3, 0, 0));
            name.setText(type);
            count.setText(String.valueOf(tags.size()));

            // 标签库主界面的标签也自适应换行（原来挤在一行里）
            FlowLayout flow = new FlowLayout(this, dp(6), dp(4));
            flow.setLayoutParams(new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
            tagRow.addView(flow);

            for (String tag : tags) {
                View chip = buildTagChip(tag, color);
                chip.setClickable(true);
                // 点标签 -> 打开编辑子界面（里面可以改名 / 改组 / 删除）
                chip.setOnClickListener(v -> showTagEditor(tag));
                flow.addView(chip);
            }

            // 与设计稿一致：点组名折叠 / 展开 长按进编辑
            head.setOnClickListener(v -> tagRow.setVisibility(
                    tagRow.getVisibility() == View.VISIBLE ? View.GONE : View.VISIBLE));
            head.setOnLongClickListener(v -> {
                showTypeEditor(type);
                return true;
            });

            dot.setOnClickListener(v -> showTypeEditor(type));

            type_list.addView(card);
        }
    }

    private LinearLayout buildTagChip(String tag, int color) {
        LinearLayout chip = new LinearLayout(this);
        chip.setOrientation(LinearLayout.HORIZONTAL);
        chip.setGravity(Gravity.CENTER_VERTICAL);
        chip.setPadding(dp(6), 0, dp(6), 0);
        chip.setBackground(rounded(Color.WHITE, 7, COLOR_BORDER, 1));

        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, dp(24));
        params.setMargins(0, 0, dp(4), dp(4));
        chip.setLayoutParams(params);

        View dot = new View(this);
        dot.setLayoutParams(new LinearLayout.LayoutParams(dp(7), dp(7)));
        dot.setBackground(rounded(color, 2, 0, 0));
        chip.addView(dot);

        TextView text = new TextView(this);
        text.setText(tag);
        text.setTextSize(10);
        text.setTextColor(COLOR_TEXT);
        text.setSingleLine(true);
        text.setEllipsize(android.text.TextUtils.TruncateAt.END);
        text.setMaxWidth(dp(110));

        LinearLayout.LayoutParams textParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        textParams.setMargins(dp(4), 0, 0, 0);
        text.setLayoutParams(textParams);

        chip.addView(text);
        return chip;
    }

    // 可移除的标签芯片：点整块就把这个标签从文件 / 容器上删除
    private View buildChip(String tag, int color, boolean removable, final Runnable on_remove) {
        LinearLayout chip = buildTagChip(tag, color);

        if (!removable) {
            return chip;
        }

        TextView remove = new TextView(this);
        remove.setText("×");
        remove.setTextSize(12);
        remove.setTextColor(COLOR_MUTED);
        remove.setGravity(Gravity.CENTER);
        remove.setLayoutParams(new LinearLayout.LayoutParams(dp(18), dp(18)));
        chip.addView(remove);

        chip.setClickable(true);
        chip.setOnClickListener(v -> on_remove.run());

        return chip;
    }

    // 标签库里的可选芯片：未选显示 ＋ 已选显示 ✓
    private View buildPickableChip(String tag, int color, boolean picked) {
        LinearLayout chip = buildTagChip(tag, color);

        TextView mark = new TextView(this);
        mark.setText(picked ? "✓" : "＋");
        mark.setTextSize(11);
        mark.setTextColor(picked ? 0xFF4D9B6A : COLOR_MUTED);
        mark.setPadding(dp(4), 0, 0, 0);
        chip.addView(mark);

        if (picked) {
            chip.setBackground(rounded(COLOR_ACTIVE_BG, 7, COLOR_BORDER, 1));
        }

        chip.setClickable(true);
        return chip;
    }

    private View buildHint(String text) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextSize(11);
        view.setTextColor(COLOR_MUTED);
        view.setPadding(dp(4), dp(6), dp(4), dp(6));
        return view;
    }

    private View buildPlainChip(String label) {
        TextView view = new TextView(this);
        view.setText(label);
        view.setTextSize(10);
        view.setTextColor(COLOR_MUTED);
        view.setPadding(dp(6), dp(6), dp(6), dp(4));
        return view;
    }

    private int colorOfTag(String tag) {
        for (Map.Entry<String, List<String>> entry : tag_types.entrySet()) {
            if (entry.getValue().contains(tag)) {
                return colorOfType(entry.getKey());
            }
        }

        return COLOR_PINK;
    }

    private int colorOfType(String type) {
        String value = tag_colors.get(type);

        if (value == null || value.isEmpty()) {
            return COLOR_PINK;
        }

        try {
            return Color.parseColor(value);
        } catch (IllegalArgumentException error) {
            try {
                return Color.parseColor(FALLBACK_COLOR);
            } catch (IllegalArgumentException ignored) {
                return COLOR_PINK;
            }
        }
    }

    private GradientDrawable rounded(int color, int radius_dp, int border_color, int border_dp) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setCornerRadius(dp(radius_dp));
        drawable.setColor(color);

        if (border_dp > 0) {
            drawable.setStroke(dp(border_dp), border_color);
        }

        return drawable;
    }

    private int dp(int value) {
        return Math.round(getResources().getDisplayMetrics().density * value);
    }

    private static String formatSize(long size) {
        if (size < 1024) {
            return size + " B";
        }
        if (size < 1024 * 1024) {
            return (size / 1024) + " KB";
        }
        if (size < 1024L * 1024 * 1024) {
            return (size / (1024 * 1024)) + " MB";
        }

        return (size / (1024L * 1024 * 1024)) + " GB";
    }

    // 浏览：搜索

    private void doSearch() {
        final List<String> include = new ArrayList<>(include_tags);
        final List<String> exclude = new ArrayList<>(exclude_tags);
        final List<String> only = new ArrayList<>(only_tags);

        runAction(Lang.get("browse.search"), () -> {
            FileDatabase.SearchOptions options = new FileDatabase.SearchOptions();
            options.include.addAll(include);
            options.exclude.addAll(exclude);
            options.only.addAll(only);

            final List<FileDatabase.FileInfo> result = new ArrayList<>(serve.searchByTags(options));

            StringBuilder summary = new StringBuilder();
            if (!include.isEmpty()) {
                summary.append(Lang.get("browse.summary_include")).append(include);
            }
            if (!exclude.isEmpty()) {
                summary.append(summary.length() > 0 ? "　" : "").append(Lang.get("browse.summary_exclude")).append(exclude);
            }
            if (!only.isEmpty()) {
                summary.append(summary.length() > 0 ? "　" : "").append(Lang.get("browse.summary_only")).append(only);
            }

            final String label = summary.length() == 0 ? Lang.get("browse.summary_none") : summary.toString();

            runOnUiThread(() -> {
                search_mode = true;
                search_summary = label;
                current_files.clear();
                current_files.addAll(result);

                toast(Lang.get("browse.found_prefix") + result.size() + Lang.get("common.count_suffix"));
                renderFiles(result);
            });
        });
    }

    private void clearSearch() {
        include_tags.clear();
        exclude_tags.clear();
        only_tags.clear();
        renderSearchChips();

        search_mode = false;
        search_summary = "";
        reload();
    }

    // 把已选标签画到三个搜索容器上
    private void renderSearchChips() {
        edit_include.setText(include_tags.isEmpty()
                ? Lang.get("browse.include_placeholder")
                : Lang.get("browse.include_prefix") + String.join("、", include_tags));

        edit_exclude.setText(exclude_tags.isEmpty()
                ? Lang.get("browse.exclude_placeholder")
                : Lang.get("browse.exclude_prefix") + String.join("、", exclude_tags));

        edit_only.setText(only_tags.isEmpty()
                ? Lang.get("browse.only_placeholder")
                : Lang.get("browse.only_prefix") + String.join("、", only_tags));
    }

    // 搜索筛选标签
    private void showSearchTagOverlay(int initial_group) {
        active_filter_group = initial_group;

        overlay_host.removeAllViews();
        search_tag_overlay = getLayoutInflater().inflate(R.layout.overlay_search_tags, overlay_host, false);
        overlay_host.addView(search_tag_overlay);
        overlay_host.setVisibility(View.VISIBLE);

        tags_include = search_tag_overlay.findViewById(R.id.tagsInclude);
        tags_exclude = search_tag_overlay.findViewById(R.id.tagsExclude);
        tags_only = search_tag_overlay.findViewById(R.id.tagsOnly);

        title_include = search_tag_overlay.findViewById(R.id.titleInclude);
        title_exclude = search_tag_overlay.findViewById(R.id.titleExclude);
        title_only = search_tag_overlay.findViewById(R.id.titleOnly);

        tv_current_group = search_tag_overlay.findViewById(R.id.tvSearchTagCurrentGroup);
        search_tag_library = search_tag_overlay.findViewById(R.id.searchTagLibrary);

        group_include = search_tag_overlay.findViewById(R.id.groupInclude);
        group_exclude = search_tag_overlay.findViewById(R.id.groupExclude);
        group_only = search_tag_overlay.findViewById(R.id.groupOnly);

        // 点容器标题 -> 切换「当前容器」
        search_tag_overlay.findViewById(R.id.headInclude).setOnClickListener(v -> setActiveFilterGroup(0));
        search_tag_overlay.findViewById(R.id.headExclude).setOnClickListener(v -> setActiveFilterGroup(1));
        search_tag_overlay.findViewById(R.id.headOnly).setOnClickListener(v -> setActiveFilterGroup(2));

        search_tag_overlay.findViewById(R.id.btnSearchTagCancel).setOnClickListener(v -> hideOverlay());
        search_tag_overlay.findViewById(R.id.btnSearchTagConfirm).setOnClickListener(v -> {
            renderSearchChips();
            hideOverlay();
            toast(Lang.get("filter.updated"));
        });

        applyOverlayTexts();
        renderFilterGroups();
        renderLibraryInto(search_tag_library, 1);
    }

    private void setActiveFilterGroup(int group) {
        active_filter_group = group;
        renderFilterGroups();
    }

    private Set<String> activeFilterSet() {
        if (active_filter_group == 1) {
            return exclude_tags;
        }

        if (active_filter_group == 2) {
            return only_tags;
        }

        return include_tags;
    }

    private static String filterGroupName(int group) {
        if (group == 1) {
            return Lang.get("filter.group_exclude");
        }

        if (group == 2) {
            return Lang.get("filter.group_only");
        }

        return Lang.get("filter.group_include");
    }

    private boolean isFilterTag(String tag) {
        return include_tags.contains(tag) || exclude_tags.contains(tag) || only_tags.contains(tag);
    }

    // 标签库点一下：加入当前容器 再点一下从容器里移除
    // 同一个标签只会待在其中一个容器里
    private void addTagToActiveFilter(String tag) {
        Set<String> target = activeFilterSet();

        if (target.contains(tag)) {
            target.remove(tag);
        } else {
            include_tags.remove(tag);
            exclude_tags.remove(tag);
            only_tags.remove(tag);
            target.add(tag);
        }

        renderFilterGroups();
        renderLibraryInto(search_tag_library, 1);
    }

    private void renderFilterGroups() {
        if (search_tag_overlay == null) {
            return;
        }

        fillFilterGroup(0, tags_include, title_include, group_include);
        fillFilterGroup(1, tags_exclude, title_exclude, group_exclude);
        fillFilterGroup(2, tags_only, title_only, group_only);

        if (tv_current_group != null) {
            tv_current_group.setText(Lang.get("filter.current_prefix") + filterGroupName(active_filter_group));
        }
    }

    private void fillFilterGroup(int group, LinearLayout container, TextView title, MaterialCardView card) {

        Set<String> tags = group == 1 ? exclude_tags : (group == 2 ? only_tags : include_tags);

        title.setText(filterGroupName(group) + Lang.get("filter.tags_suffix") + tags.size());

        if (group == active_filter_group) {
            title.setTextColor(COLOR_TEXT);
            card.setStrokeWidth(dp(2));
        } else {
            title.setTextColor(0xFF59616E);
            card.setStrokeWidth(dp(1));
        }

        // 用矢量图标代替
        setCompoundIcon(title, group == 0 ? R.drawable.funnel_plus : (group == 1 ? R.drawable.funnel_x : R.drawable.funnel), group == active_filter_group ? COLOR_TEXT : 0xFF59616E, 12);

        container.removeAllViews();

        if (tags.isEmpty()) {
            container.addView(buildHint(Lang.get("filter.empty")));
            return;
        }

        for (String tag : tags) {
            container.addView(buildChip(tag, colorOfTag(tag), true, () -> {
                Set<String> current = group == 1 ? exclude_tags : (group == 2 ? only_tags : include_tags);
                current.remove(tag);
                renderFilterGroups();
                renderLibraryInto(search_tag_library, 1);
            }));
        }
    }

    // 标签库渲染
    // mode 0：给文件挑标签  mode 1：给搜索容器挑标签
    private void renderLibraryInto(LinearLayout container, final int mode) {
        if (container == null) {
            return;
        }

        container.removeAllViews();

        if (tag_types.isEmpty()) {
            container.addView(buildHint(Lang.get("filter.library_empty")));
            return;
        }

        for (Map.Entry<String, List<String>> entry : tag_types.entrySet()) {
            final String type = entry.getKey();
            final List<String> tags = entry.getValue();
            final int color = colorOfType(type);

            View card = getLayoutInflater().inflate(R.layout.item_type, container, false);

            View dot = card.findViewById(R.id.typeDot);
            TextView name = card.findViewById(R.id.typeName);
            TextView count = card.findViewById(R.id.typeCount);
            final LinearLayout tagRow = card.findViewById(R.id.typeTags);

            dot.setBackground(rounded(color, 3, 0, 0));
            name.setText(type);
            count.setText(String.valueOf(tags.size()));

            card.findViewById(R.id.typeHead).setOnClickListener(v -> tagRow.setVisibility(
                    tagRow.getVisibility() == View.VISIBLE ? View.GONE : View.VISIBLE));

            for (final String tag : tags) {
                final boolean picked = mode == 0 ? file_tag_working.contains(tag) : isFilterTag(tag);

                View chip = buildPickableChip(tag, color, picked);
                chip.setOnClickListener(v -> {
                    if (mode == 0) {
                        if (file_tag_working.contains(tag)) {
                            file_tag_working.remove(tag);
                        } else {
                            file_tag_working.add(tag);
                        }

                        renderFileTagCurrent();
                        renderLibraryInto(file_tag_library, 0);
                    } else {
                        addTagToActiveFilter(tag);
                    }
                });

                tagRow.addView(chip);
            }

            container.addView(card);
        }
    }

    private void hideOverlay() {
        overlay_host.removeAllViews();
        overlay_host.setVisibility(View.GONE);

        file_tag_overlay = null;
        search_tag_overlay = null;
        tag_editor_overlay = null;
        dir_manage_overlay = null;
        dir_manage_list = null;
        tv_dir_manage_empty = null;
        dir_browser_overlay = null;
        dir_browser_crumbs = null;
        dir_browser_crumb_row = null;
        dir_browser_list = null;
        dir_browser_empty = null;
        file_tag_target = null;
        editor_selected_type = null;
        editor_selected_tag = null;
    }

    // 目录

    private void pickRoot() {
        // 只有「所有文件访问」这一种方式：在应用内自己浏览目录
        // 完全不碰系统选择器（测试机上的选择器会被 ROM 卡死）
        // 初始目录固定是内部存储根（/storage/emulated/0）
        if (!hasAllFilesAccess()) {
            showAllFilesPermissionDialog();
            return;
        }

        showDirBrowserOverlay(Environment.getExternalStorageDirectory());
    }

    // 目录浏览子界面（面包屑 + 新建文件夹 + 选择此目录）

    private void showDirBrowserOverlay(File start) {
        dir_browser_path = start;

        overlay_host.removeAllViews();
        dir_browser_overlay = getLayoutInflater().inflate(R.layout.overlay_dir_browser, overlay_host, false);
        overlay_host.addView(dir_browser_overlay);
        overlay_host.setVisibility(View.VISIBLE);

        dir_browser_crumbs = dir_browser_overlay.findViewById(R.id.dirBrowserCrumbs);
        dir_browser_crumb_row = dir_browser_overlay.findViewById(R.id.dirBrowserCrumbRow);
        dir_browser_list = dir_browser_overlay.findViewById(R.id.dirBrowserList);
        dir_browser_empty = dir_browser_overlay.findViewById(R.id.dirBrowserEmpty);

        dir_browser_overlay.findViewById(R.id.btnDirBrowserBack).setOnClickListener(v -> hideOverlay());
        dir_browser_overlay.findViewById(R.id.btnDirBrowserUp).setOnClickListener(v -> browseUp());
        dir_browser_overlay.findViewById(R.id.btnDirBrowserNew).setOnClickListener(v -> createFolderHere());
        dir_browser_overlay.findViewById(R.id.btnDirBrowserChoose).setOnClickListener(v -> chooseBrowserFolder());

        applyOverlayTexts();
        renderDirBrowser();
    }

    private void renderDirBrowser() {
        if (dir_browser_list == null || dir_browser_path == null) {
            return;
        }

        renderCrumbs();

        TextView current = dir_browser_overlay.findViewById(R.id.tvDirBrowserCurrent);
        current.setText(Lang.get("dir.browser_current") + dir_browser_path.getAbsolutePath());

        dir_browser_list.removeAllViews();

        File[] children = dir_browser_path.listFiles();

        if (children == null || children.length == 0) {
            dir_browser_empty.setVisibility(View.VISIBLE);
            return;
        }

        dir_browser_empty.setVisibility(View.GONE);

        // 文件夹在前 其余按名字排
        Arrays.sort(children, (left, right) -> {
            if (left.isDirectory() != right.isDirectory()) {
                return left.isDirectory() ? -1 : 1;
            }

            return left.getName().compareToIgnoreCase(right.getName());
        });

        // 不过滤任何目录：以 . 开头的隐藏文件夹也要能选（比如 .nomedia 下面存的东西）
        for (final File child : children) {
            boolean directory = child.isDirectory();
            View row = getLayoutInflater().inflate(R.layout.item_dir_browse, dir_browser_list, false);

            ImageView icon = row.findViewById(R.id.dirBrowseIcon);
            TextView name = row.findViewById(R.id.dirBrowseName);
            TextView meta = row.findViewById(R.id.dirBrowseMeta);
            TextView arrow = row.findViewById(R.id.dirBrowseArrow);

            name.setText(child.getName());

            if (directory) {
                // 统一风格：跟目录页 / 文件列表一样 中性色图标 + 浅底
                icon.setImageResource(R.drawable.folder);
                icon.setImageTintList(ColorStateList.valueOf(0xFF59616E));
                icon.setBackground(rounded(COLOR_ACTIVE_BG, 11, 0, 0));

                File[] inner = child.listFiles();
                meta.setText(Lang.f("dir.browser_items", inner == null ? 0 : inner.length)
                        + " · " + formatDate(child.lastModified()));

                row.setOnClickListener(v -> {
                    dir_browser_path = child;
                    renderDirBrowser();
                });
            } else {
                icon.setImageResource(R.drawable.file_text);
                icon.setImageTintList(ColorStateList.valueOf(COLOR_MUTED));
                icon.setBackground(rounded(COLOR_ACTIVE_BG, 11, 0, 0));
                meta.setText(formatSize(child.length()) + " · " + formatDate(child.lastModified()));
                arrow.setVisibility(View.GONE);
                row.setAlpha(0.55f);
            }

            dir_browser_list.addView(row);
        }
    }

    // 面包屑：点任意一级直接跳过去
    private void renderCrumbs() {
        dir_browser_crumb_row.removeAllViews();

        List<File> chain = new ArrayList<>();
        File cursor = dir_browser_path;

        while (cursor != null) {
            chain.add(0, cursor);
            cursor = cursor.getParentFile();
        }

        for (int i = 0; i < chain.size(); i++) {
            final File target = chain.get(i);
            boolean current = i == chain.size() - 1;

            if (i > 0) {
                TextView sep = new TextView(this);
                sep.setText("›");
                sep.setTextSize(10);
                sep.setTextColor(COLOR_BORDER);
                sep.setPadding(dp(4), 0, dp(4), 0);
                dir_browser_crumb_row.addView(sep);
            }

            TextView crumb = new TextView(this);
            crumb.setText(target.getName().isEmpty() ? "/" : target.getName());
            crumb.setTextSize(11);
            crumb.setPadding(dp(8), dp(4), dp(8), dp(4));
            crumb.setTextColor(current ? COLOR_TEXT : COLOR_MUTED);
            crumb.setTypeface(null, current ? Typeface.BOLD : Typeface.NORMAL);
            crumb.setBackground(rounded(current ? COLOR_ACTIVE_BG : Color.TRANSPARENT, 6, 0, 0));
            crumb.setOnClickListener(v -> {
                dir_browser_path = target;
                renderDirBrowser();
            });

            dir_browser_crumb_row.addView(crumb);
        }

        // 每次重画都滚到最右边 当前目录才看得见
        dir_browser_crumbs.post(() -> dir_browser_crumbs.fullScroll(View.FOCUS_RIGHT));
    }

    private void browseUp() {
        File parent = dir_browser_path == null ? null : dir_browser_path.getParentFile();

        if (parent == null) {
            toast(Lang.get("dir.browser_top"));
            return;
        }

        dir_browser_path = parent;
        renderDirBrowser();
    }

    // 在当前目录里新建文件夹
    private void createFolderHere() {
        final EditText input = new EditText(this);
        input.setSingleLine(true);
        input.setHint(Lang.get("dir.new_folder_hint"));
        input.setPadding(dp(12), dp(10), dp(12), dp(10));

        new AlertDialog.Builder(this)
                .setTitle(Lang.get("dir.browser_new_folder"))
                .setView(input)
                .setPositiveButton(Lang.get("common.create"), (dialog, which) -> {
                    String name = input.getText().toString().trim();

                    if (name.isEmpty() || name.indexOf('/') >= 0) {
                        toast(Lang.get("dir.new_folder_invalid"));
                        return;
                    }

                    File target = new File(dir_browser_path, name);

                    if (target.exists()) {
                        toast(Lang.get("dir.new_folder_exists"));
                        return;
                    }

                    if (!target.mkdirs()) {
                        toast(Lang.get("dir.new_folder_failed"));
                        return;
                    }

                    renderDirBrowser();
                    toast(Lang.get("dir.new_folder_done_prefix") + name);
                })
                .setNegativeButton(Lang.get("common.cancel"), null)
                .show();
    }

    // 选择当前目录 -> 加进受管理目录
    private void chooseBrowserFolder() {
        if (dir_browser_path == null) {
            return;
        }

        File target = dir_browser_path;

        hideOverlay();
        addFilesRoot(target);
    }

    private static String formatDate(long time) {
        if (time <= 0L) {
            return "";
        }

        return new SimpleDateFormat("yyyy-MM-dd", Locale.getDefault()).format(new Date(time));
    }

    // 把一个绝对路径加进受管理目录
    private void addFilesRoot(final File directory) {
        final String path = StorageAccess.normalizePath(directory.getAbsolutePath());

        runAction(Lang.get("dir.action_add"), () -> {
            boolean ok = serve.addRoot(Uri.fromFile(new File(path)));
            toast(ok ? Lang.get("dir.added") : Lang.get("common.add_failed") + serve.getLastError());
        });
    }

    // 「所有文件访问」权限
    // API 30+ 是 special access 只能去系统设置里开（manifest 里必须先声明 MANAGE_EXTERNAL_STORAGE）
    // API < 30 没有这个开关 用运行时读写权限代替
    private boolean hasAllFilesAccess() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            return checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
                    == PackageManager.PERMISSION_GRANTED
                    && checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)
                    == PackageManager.PERMISSION_GRANTED;
        }

        return Environment.isExternalStorageManager();
    }

    // logcat 诊断用：把本应用持有的持久化授权全打出来（adb logcat -s TagMeow）
    private void logPersistedGrants() {
        try {
            for (UriPermission permission : getContentResolver().getPersistedUriPermissions()) {
                Log.i(TAG, "persisted grant: " + permission.getUri()
                        + " read=" + permission.isReadPermission()
                        + " write=" + permission.isWritePermission());
            }
        } catch (RuntimeException error) {
            Log.w(TAG, "cannot read persisted uri permissions", error);
        }
    }

    private void showAllFilesPermissionDialog() {
        new AlertDialog.Builder(this)
                .setTitle(Lang.get("storage.perm_title"))
                .setMessage(Lang.get("storage.perm_body"))
                .setPositiveButton(Lang.get("storage.perm_go"), (dialog, which) -> openAllFilesSettings())
                .setNegativeButton(Lang.get("common.cancel"), null)
                .show();
    }

    private void openAllFilesSettings() {
        // API < 30 没有「所有文件访问」设置页 直接要运行时权限
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            all_files_permission_picker.launch(new String[]{
                    Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE});
            return;
        }

        pending_all_files_request = true;

        try {
            startActivity(new Intent(
                    Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName())));
        } catch (ActivityNotFoundException error) {
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            } catch (ActivityNotFoundException ignored) {
                toast(Lang.get("storage.perm_unavailable"));
            }
        }
    }

    // 设置页里的存储方式：只剩一种 不能再切
    // 点进去只是说明现在为什么只有这一种
    private void showStorageModeInfo() {
        new AlertDialog.Builder(this)
                .setTitle(Lang.get("settings.storage_mode"))
                .setMessage(storageModeLabel())
                .setPositiveButton(Lang.get("common.ok"), null)
                .show();
    }

    // 权限刚拿到之后重建引擎（目录配置、索引、扫描全跟着来）
    private void restartEngine() {
        initEngine();
        renderSettings();
    }

    @Override
    protected void onResume() {
        super.onResume();

        // 从「所有文件访问」设置页回来
        if (pending_all_files_request) {
            pending_all_files_request = false;

            if (hasAllFilesAccess()) {
                restartEngine();
            } else {
                toast(Lang.get("storage.perm_denied"));
            }
        }
    }

    // 这个目录的授权还在不在：真的去读一次（只在 worker 线程调用）
    // 存储层还不认识这个 root（比如刚读配置）就先注册一下
    private boolean hasAccess(DirectoryConfigManager.Directory directory) {
        StorageAccess current = storage;

        if (current == null) {
            return false;
        }

        FileRef root = new FileRef(directory.getId(), "");

        if (current.locatorOf(root) == null) {
            current.addRoot(directory.getId(), storageLocatorOf(directory.getUri()));
        }

        boolean ok = current.exists(root) && current.isDirectory(root);

        if (!ok) {
            // adb logcat -s TagMeow：根目录读不了的真正原因（权限被回收 / 目录被删）
            Log.w(TAG, "root not accessible: " + directory.getId() + " uri=" + directory.getUri() + " reason=" + current.getLastError());
        }

        return ok;
    }

    // Uri -> 存储层定位串（file Uri 用纯路径 跟 TagServe 保持一致）
    private static String storageLocatorOf(Uri uri) {
        if ("file".equalsIgnoreCase(uri.getScheme())) {
            String path = uri.getPath();

            if (path != null && !path.isEmpty()) {
                return StorageAccess.normalizePath(path);
            }
        }

        return uri.toString();
    }

    // 目录管理子界面：列表多选 + 打开 / 刷新 / 删除

    private void showDirManageOverlay() {
        showDirManageOverlay(null);
    }

    private void showDirManageOverlay(String preselect_root_id) {
        selected_root_ids.clear();

        if (preselect_root_id != null) {
            selected_root_ids.add(preselect_root_id);
        }

        overlay_host.removeAllViews();
        dir_manage_overlay = getLayoutInflater().inflate(R.layout.overlay_dir_manage, overlay_host, false);
        overlay_host.addView(dir_manage_overlay);
        overlay_host.setVisibility(View.VISIBLE);

        dir_manage_list = dir_manage_overlay.findViewById(R.id.dirManageList);
        tv_dir_manage_empty = dir_manage_overlay.findViewById(R.id.tvDirManageEmpty);

        dir_manage_overlay.findViewById(R.id.btnDirManageBack).setOnClickListener(v -> hideOverlay());
        dir_manage_overlay.findViewById(R.id.btnDirManageOpen).setOnClickListener(v -> openSelectedRoot());
        dir_manage_overlay.findViewById(R.id.btnDirManageRefresh).setOnClickListener(v -> refreshAllRoots());
        dir_manage_overlay.findViewById(R.id.btnDirManageDelete).setOnClickListener(v -> deleteSelectedRoots());

        applyOverlayTexts();
        renderDirManageList();
    }

    private void renderDirManageList() {
        if (dir_manage_list == null) {
            return;
        }

        dir_manage_list.removeAllViews();
        tv_dir_manage_empty.setVisibility(current_roots.isEmpty() ? View.VISIBLE : View.GONE);

        for (final DirectoryConfigManager.Directory directory : current_roots) {
            boolean granted = Boolean.TRUE.equals(root_access.get(directory.getId()));
            boolean selected = selected_root_ids.contains(directory.getId());

            View row = getLayoutInflater().inflate(R.layout.item_dir_manage, dir_manage_list, false);

            // 选择框：选中就填色并显示对勾
            TextView select = row.findViewById(R.id.dirSelect);
            select.setText(selected ? "✓" : "");
            select.setBackground(rounded(
                    selected ? COLOR_TEXT : Color.WHITE,
                    7,
                    selected ? COLOR_TEXT : COLOR_BORDER,
                    2));

            ImageView icon = row.findViewById(R.id.dirManageIcon);
            icon.setImageResource(R.drawable.folder);
            icon.setImageTintList(ColorStateList.valueOf(0xFF59616E));
            icon.setBackground(rounded(COLOR_ACTIVE_BG, 10, 0, 0));

            TextView name = row.findViewById(R.id.dirManageName);
            name.setText(directory.getDisplayName());

            TextView path = row.findViewById(R.id.dirManagePath);
            path.setText(displayPathOf(directory));

            // 有效 / 无效：跟「真的读一次」的结果一致
            TextView badge = row.findViewById(R.id.dirBadge);
            badge.setText(granted ? Lang.get("dir.badge_valid") : Lang.get("dir.badge_invalid"));
            badge.setTextColor(granted ? 0xFF4E93C8 : COLOR_MUTED);
            badge.setBackground(rounded(
                    granted ? 0xFFEEF8FF : COLOR_ACTIVE_BG,
                    6,
                    granted ? 0xFF8EC8F7 : COLOR_BORDER,
                    1));

            row.setOnClickListener(v -> {
                if (selected_root_ids.contains(directory.getId())) {
                    selected_root_ids.remove(directory.getId());
                } else {
                    selected_root_ids.add(directory.getId());
                }

                renderDirManageList();
            });

            // ⋮：单个目录的更多操作
            row.findViewById(R.id.dirManageMore).setOnClickListener(v -> showRootActions(directory));

            dir_manage_list.addView(row);
        }
    }

    // 列表第二行：路径模式下给绝对路径 SAF 下给 tree 路径
    private static String displayPathOf(DirectoryConfigManager.Directory directory) {
        Uri uri = directory.getUri();

        if ("file".equalsIgnoreCase(uri.getScheme())) {
            String path = uri.getPath();

            return path == null || path.isEmpty() ? directory.getId() : path;
        }

        return directory.getId();
    }

    // 打开：只能选一个
    private void openSelectedRoot() {
        if (selected_root_ids.isEmpty()) {
            toast(Lang.get("dir.open_selected_hint"));
            return;
        }

        if (selected_root_ids.size() > 1) {
            toast(Lang.get("dir.open_single_hint"));
            return;
        }

        current_directory = new FileRef(selected_root_ids.iterator().next(), "");
        search_mode = false;

        hideOverlay();
        selectTab(0);
    }

    // 刷新：重扫全部受管理目录
    private void refreshAllRoots() {
        runAction(Lang.get("dir.action_refresh"), () -> {
            boolean ok = serve.refreshAll();
            toast(ok
                    ? Lang.f("dir.refresh_done_count", serve.getFileDatabase().countFiles())
                    : Lang.get("common.refresh_failed") + serve.getLastError());
        });
    }

    // 删除：确认后只摘掉管理关系 不动真实文件
    private void deleteSelectedRoots() {
        if (selected_root_ids.isEmpty()) {
            toast(Lang.get("dir.delete_selected_hint"));
            return;
        }

        final List<String> ids = new ArrayList<>(selected_root_ids);

        new AlertDialog.Builder(this)
                .setTitle(Lang.get("dir.action_remove"))
                .setMessage(Lang.f("dir.delete_confirm", ids.size()))
                .setPositiveButton(Lang.get("common.delete"), (dialog, which) ->
                        runAction(Lang.get("dir.action_remove"), () -> {
                            boolean ok = true;

                            for (String id : ids) {
                                DirectoryConfigManager.Directory directory = findRoot(id);

                                if (directory != null) {
                                    Uri root_uri = directory.getUri();
                                    boolean removed = serve.removeRoot(root_uri);
                                    ok &= removed;

                                    if (removed) {
                                        // 顺手把持久化授权还回去 不释放的话授权会一直堆在系统里
                                        // （系统对每个应用的持久化授权数量是有上限的）
                                        try {
                                            getContentResolver().releasePersistableUriPermission(
                                                    root_uri,
                                                    Intent.FLAG_GRANT_READ_URI_PERMISSION
                                                            | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                                        } catch (SecurityException error) {
                                            Log.w(TAG, "releasePersistableUriPermission failed: " + root_uri, error);
                                        }
                                    }
                                }
                            }

                            selected_root_ids.clear();
                            toast(ok ? Lang.get("dir.removed") : Lang.get("common.remove_failed") + serve.getLastError());
                        }))
                .setNegativeButton(Lang.get("common.cancel"), null)
                .show();
    }

    private DirectoryConfigManager.Directory findRoot(String root_id) {
        for (DirectoryConfigManager.Directory directory : current_roots) {
            if (directory.getId().equals(root_id)) {
                return directory;
            }
        }

        return null;
    }

    private void showRootActions(DirectoryConfigManager.Directory directory) {
        new AlertDialog.Builder(this)
                .setTitle(directory.getDisplayName())
                .setItems(new String[]{Lang.get("dir.open"), Lang.get("dir.refresh_one"), Lang.get("dir.remove")}, (dialog, which) -> {
                    if (which == 0) {
                        current_directory = new FileRef(directory.getId(), "");
                        search_mode = false;
                        selectTab(0);
                    } else if (which == 1) {
                        runAction(Lang.get("dir.action_refresh_one"), () -> {
                            boolean ok = serve.refreshRoot(directory.getId());
                            toast(ok ? Lang.get("dir.refresh_done") : Lang.get("common.refresh_failed") + serve.getLastError());
                        });
                    } else {
                        runAction(Lang.get("dir.action_remove"), () -> {
                            boolean ok = serve.removeRoot(directory.getUri());
                            toast(ok ? Lang.get("dir.removed") : Lang.get("common.remove_failed") + serve.getLastError());
                        });
                    }
                })
                .setNegativeButton(Lang.get("common.cancel"), null)
                .show();
    }





    // 文件标签
    // 新建 / 编辑标签组
    // 新建 / 编辑 类型与标签
    // 分段切换 + 列表点击回填输入框（回填后就能改名 / 改色 / 删除）

    private void showTypeEditor(String type) {
        openTagEditor(EDITOR_MODE_TYPE);

        if (type != null) {
            pickEditorType(type);
        }
    }

    private void showTagEditor(String tag) {
        openTagEditor(EDITOR_MODE_TAG);

        if (tag != null) {
            pickEditorTag(tag);
        }
    }

    private void openTagEditor(int mode) {
        editor_mode = mode;
        editor_selected_type = null;
        editor_selected_tag = null;

        overlay_host.removeAllViews();
        tag_editor_overlay = getLayoutInflater().inflate(R.layout.overlay_tag_editor, overlay_host, false);
        overlay_host.addView(tag_editor_overlay);
        overlay_host.setVisibility(View.VISIBLE);

        editor_scroll = tag_editor_overlay.findViewById(R.id.editorScroll);
        seg_type = tag_editor_overlay.findViewById(R.id.segType);
        seg_tag = tag_editor_overlay.findViewById(R.id.segTag);
        tv_editor_title = tag_editor_overlay.findViewById(R.id.tvEditorTitle);
        form_type = tag_editor_overlay.findViewById(R.id.formType);
        form_tag = tag_editor_overlay.findViewById(R.id.formTag);

        edit_type_name = tag_editor_overlay.findViewById(R.id.editTypeName);
        edit_type_new_name = tag_editor_overlay.findViewById(R.id.editTypeNewName);
        edit_type_color = tag_editor_overlay.findViewById(R.id.editTypeColor);
        color_swatch = tag_editor_overlay.findViewById(R.id.colorSwatch);
        btn_pick_color = tag_editor_overlay.findViewById(R.id.btnPickColor);
        type_color_preview = tag_editor_overlay.findViewById(R.id.typeColorPreview);
        type_list_display = tag_editor_overlay.findViewById(R.id.typeListDisplay);

        btn_select_type = tag_editor_overlay.findViewById(R.id.btnSelectType);
        sel_type_dot = tag_editor_overlay.findViewById(R.id.selTypeDot);
        sel_type_name = tag_editor_overlay.findViewById(R.id.selTypeName);
        type_option_list = tag_editor_overlay.findViewById(R.id.typeOptionList);
        edit_tag_name = tag_editor_overlay.findViewById(R.id.editTagName);
        tag_list_display = tag_editor_overlay.findViewById(R.id.tagListDisplay);

        // 颜色：不用预设色块 点色块 / 「选色」打开选色盘
        color_swatch.setOnClickListener(v -> showColorPicker(edit_type_color));
        btn_pick_color.setOnClickListener(v -> showColorPicker(edit_type_color));

        // 默认颜色
        if (edit_type_color.getText().toString().trim().isEmpty()) {
            edit_type_color.setText(DEFAULT_TAG_COLOR);
        }

        edit_type_name.setOnFocusChangeListener((v, hasFocus) -> {
            if (!hasFocus) {
                renderTypeColorPreview();
            }
        });
        edit_type_color.setOnFocusChangeListener((v, hasFocus) -> {
            if (!hasFocus) {
                renderTypeColorPreview();
            }
        });

        seg_type.setOnClickListener(v -> switchEditorMode(EDITOR_MODE_TYPE));
        seg_tag.setOnClickListener(v -> switchEditorMode(EDITOR_MODE_TAG));

        btn_select_type.setOnClickListener(v -> {
            boolean open = type_option_list.getVisibility() != View.VISIBLE;
            type_option_list.setVisibility(open ? View.VISIBLE : View.GONE);

            if (open) {
                renderEditorTypeOptions();
            }
        });

        tag_editor_overlay.findViewById(R.id.btnEditorBack).setOnClickListener(v -> hideOverlay());
        tag_editor_overlay.findViewById(R.id.btnEditorAdd).setOnClickListener(v -> submitEditor());
        tag_editor_overlay.findViewById(R.id.btnEditorDelete).setOnClickListener(v -> deleteEditorTarget());
        tag_editor_overlay.findViewById(R.id.btnEditorRename).setOnClickListener(v -> renameEditorType());

        // 标签表单默认选中第一个类型
        if (!tag_types.isEmpty()) {
            pickEditorType(tag_types.keySet().iterator().next());
        }

        switchEditorMode(mode);

        // 子界面里的静态文案也跟着语言走
        applyOverlayTexts();
    }

    private void switchEditorMode(int mode) {
        editor_mode = mode;

        boolean isType = mode == EDITOR_MODE_TYPE;

        form_type.setVisibility(isType ? View.VISIBLE : View.GONE);
        form_tag.setVisibility(isType ? View.GONE : View.VISIBLE);

        tv_editor_title.setText(isType ? Lang.get("editor.title_type") : Lang.get("editor.title_tag"));

        seg_type.setBackground(isType ? rounded(Color.WHITE, 10, 0, 0) : null);
        seg_tag.setBackground(isType ? null : rounded(Color.WHITE, 10, 0, 0));

        seg_type.setTextColor(isType ? COLOR_TEXT : 0xFF626A75);
        seg_tag.setTextColor(isType ? 0xFF626A75 : COLOR_TEXT);

        seg_type.setTypeface(null, isType ? Typeface.BOLD : Typeface.NORMAL);
        seg_tag.setTypeface(null, isType ? Typeface.NORMAL : Typeface.BOLD);

        editor_scroll.scrollTo(0, 0);

        if (isType) {
            renderEditorTypeList();
            renderTypeColorPreview();
        } else {
            renderEditorTagList();
        }
    }

    // 类型列表：点一下把名称和颜色回填到输入框
    private void renderEditorTypeList() {
        if (type_list_display == null) {
            return;
        }

        type_list_display.removeAllViews();

        if (tag_types.isEmpty()) {
            type_list_display.addView(buildHint(Lang.get("editor.empty_types")));
            return;
        }

        // 自适应行列：类型多了自动换行 不再一行一个占满整宽
        FlowLayout flow = new FlowLayout(this, dp(6), dp(6));
        flow.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        type_list_display.addView(flow);

        for (final String type : tag_types.keySet()) {
            boolean active = type.equals(editor_selected_type);

            LinearLayout item = new LinearLayout(this);
            item.setOrientation(LinearLayout.HORIZONTAL);
            item.setGravity(Gravity.CENTER_VERTICAL);
            item.setPadding(dp(10), dp(7), dp(10), dp(7));
            item.setBackground(rounded(
                    active ? COLOR_ACTIVE_BG : Color.WHITE,
                    10,
                    active ? COLOR_TEXT : COLOR_BORDER,
                    active ? 2 : 1));

            View dot = new View(this);
            dot.setLayoutParams(new LinearLayout.LayoutParams(dp(9), dp(9)));
            dot.setBackground(rounded(colorOfType(type), 3, 0, 0));
            item.addView(dot);

            TextView name = new TextView(this);
            name.setText(type);
            name.setTextSize(12);
            name.setTextColor(COLOR_TEXT);
            name.setPadding(dp(8), 0, dp(4), 0);
            item.addView(name);

            if (active) {
                TextView check = new TextView(this);
                check.setText("✓");
                check.setTextSize(12);
                check.setTextColor(0xFF4D9B6A);
                item.addView(check);
            }

            item.setOnClickListener(v -> pickEditorType(type));
            flow.addView(item);
        }
    }

    // 标签表单里的类型下拉列表
    private void renderEditorTypeOptions() {
        if (type_option_list == null) {
            return;
        }

        type_option_list.removeAllViews();

        for (final String type : tag_types.keySet()) {
            boolean active = type.equals(editor_selected_type);

            LinearLayout option = new LinearLayout(this);
            option.setOrientation(LinearLayout.HORIZONTAL);
            option.setGravity(Gravity.CENTER_VERTICAL);
            option.setPadding(dp(12), dp(10), dp(12), dp(10));
            option.setBackgroundColor(active ? COLOR_ACTIVE_BG : Color.WHITE);

            View dot = new View(this);
            dot.setLayoutParams(new LinearLayout.LayoutParams(dp(9), dp(9)));
            dot.setBackground(rounded(colorOfType(type), 3, 0, 0));
            option.addView(dot);

            TextView name = new TextView(this);
            name.setText(type);
            name.setTextSize(12);
            name.setTextColor(COLOR_TEXT);
            name.setPadding(dp(8), 0, 0, 0);
            option.addView(name);

            Space space = new Space(this);
            space.setLayoutParams(new LinearLayout.LayoutParams(0, 1, 1f));
            option.addView(space);

            if (active) {
                TextView check = new TextView(this);
                check.setText("✓");
                check.setTextSize(12);
                check.setTextColor(0xFF4D9B6A);
                option.addView(check);
            }

            option.setOnClickListener(v -> pickEditorType(type));
            type_option_list.addView(option);
        }
    }

    private void renderEditorTagList() {
        if (tag_list_display == null) {
            return;
        }

        tag_list_display.removeAllViews();

        // 选中的类型可能已经被删掉了 这里纠正一下
        if (editor_selected_type == null || !tag_types.containsKey(editor_selected_type)) {
            editor_selected_type = tag_types.isEmpty()
                    ? null
                    : tag_types.keySet().iterator().next();

            if (editor_selected_type != null) {
                sel_type_name.setText(editor_selected_type);
                sel_type_dot.setBackground(rounded(colorOfType(editor_selected_type), 3, 0, 0));
            } else {
                sel_type_name.setText("");
            }
        }

        if (editor_selected_type == null) {
            tag_list_display.addView(buildHint(Lang.get("editor.pick_group_first")));
            return;
        }

        List<String> tags = tag_types.get(editor_selected_type);

        if (tags == null || tags.isEmpty()) {
            tag_list_display.addView(buildHint(Lang.get("editor.empty_tags")));
            return;
        }

        // 新建标签里的「该类型下的标签」同样自适应换行：标签多了排成多行
        FlowLayout flow = new FlowLayout(this, dp(6), dp(6));
        flow.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        tag_list_display.addView(flow);

        for (final String tag : tags) {
            View chip = buildPickableChip(tag, colorOfType(editor_selected_type), tag.equals(editor_selected_tag));
            chip.setOnClickListener(v -> pickEditorTag(tag));
            flow.addView(chip);
        }
    }

    // 回填：类型表单里选类型 = 编辑它 标签表单里选类型 = 换所属组
    private void pickEditorType(String type) {
        editor_selected_type = type;

        if (type != null && editor_mode == EDITOR_MODE_TYPE) {
            edit_type_name.setText(type);
            edit_type_color.setText(tag_colors.containsKey(type) ? tag_colors.get(type) : hexOf(COLOR_ACCENT));
            renderTypeColorPreview();
        }

        if (type != null) {
            sel_type_name.setText(type);
            sel_type_dot.setBackground(rounded(colorOfType(type), 3, 0, 0));
        }

        type_option_list.setVisibility(View.GONE);

        if (editor_mode == EDITOR_MODE_TYPE) {
            renderEditorTypeList();
        } else {
            renderEditorTagList();
        }
    }

    private void pickEditorTag(String tag) {
        editor_selected_tag = tag;

        if (tag != null) {
            edit_tag_name.setText(tag);
        }

        renderEditorTagList();
    }

    // 重命名类型：先在类型列表里点一个类型 再在「新类型名称」里填新名字
    // 只改标签库 已经写进文件的标签不动
    private void renameEditorType() {
        if (editor_selected_type == null) {
            toast(Lang.get("editor.rename_type_hint"));
            return;
        }

        final String oldName = editor_selected_type;
        final String newName = edit_type_new_name == null
                ? ""
                : edit_type_new_name.getText().toString().trim();

        if (newName.isEmpty()) {
            toast(Lang.get("editor.name_required"));
            edit_type_new_name.requestFocus();
            return;
        }

        if (newName.equals(oldName)) {
            return;
        }

        if (tag_types.containsKey(newName)) {
            toast(Lang.get("editor.rename_exists"));
            return;
        }

        runAction(Lang.get("editor.rename_type_action"), () -> {
            boolean ok = serve.renameType(oldName, newName);

            if (ok) {
                ok = serve.saveTag();
            }

            if (!ok) {
                toast(Lang.get("common.rename_failed") + serve.getLastError());
                return;
            }

            // 主线程上把选中项挪到新名字 再让 reload() 重画（FIFO 顺序有保证）
            runOnUiThread(() -> {
                editor_selected_type = newName;
                edit_type_name.setText(newName);
                edit_type_new_name.setText("");
            });

            toast(Lang.get("editor.type_renamed_prefix") + newName);
        });
    }


    private void renderTypeColorPreview() {
        int color = parseColorOrDefault(edit_type_color.getText().toString().trim());
        String name = edit_type_name.getText().toString().trim();

        type_color_preview.removeAllViews();
        type_color_preview.addView(buildTagChip(name.isEmpty() ? Lang.get("editor.preview_default") : name, color));

        if (color_swatch != null) {
            color_swatch.setBackground(rounded(color, 10, COLOR_BORDER, 1));
        }
    }

    // 选色盘：色相 / 饱和度 / 明度
    private void showColorPicker(final EditText target) {
        final float[] hsv = new float[3];
        Color.colorToHSV(parseColorOrDefault(target.getText().toString().trim()), hsv);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(20), dp(14), dp(20), dp(4));

        final View swatch = new View(this);
        LinearLayout.LayoutParams swatchParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(48));
        swatchParams.setMargins(0, 0, 0, dp(8));
        swatch.setLayoutParams(swatchParams);
        root.addView(swatch);

        final TextView hex_label = new TextView(this);
        hex_label.setTextSize(13);
        hex_label.setTextColor(COLOR_TEXT);
        hex_label.setGravity(Gravity.CENTER);
        hex_label.setPadding(0, 0, 0, dp(8));
        root.addView(hex_label);

        final SeekBar hue_bar = new SeekBar(this);
        hue_bar.setMax(360);
        hue_bar.setProgress(Math.round(hsv[0]));

        final SeekBar sat_bar = new SeekBar(this);
        sat_bar.setMax(100);
        sat_bar.setProgress(Math.round(hsv[1] * 100));

        final SeekBar val_bar = new SeekBar(this);
        val_bar.setMax(100);
        val_bar.setProgress(Math.round(hsv[2] * 100));

        root.addView(labelFor(Lang.get("editor.color_hue")));
        root.addView(hue_bar);
        root.addView(labelFor(Lang.get("editor.color_sat")));
        root.addView(sat_bar);
        root.addView(labelFor(Lang.get("editor.color_value")));
        root.addView(val_bar);

        SeekBar.OnSeekBarChangeListener listener = new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar bar, int progress, boolean from_user) {
                refreshColorPicker(hsv, swatch, hex_label, hue_bar, sat_bar, val_bar);
            }

            @Override
            public void onStartTrackingTouch(SeekBar bar) {
                // 不需要处理
            }

            @Override
            public void onStopTrackingTouch(SeekBar bar) {
                // 不需要处理
            }
        };

        hue_bar.setOnSeekBarChangeListener(listener);
        sat_bar.setOnSeekBarChangeListener(listener);
        val_bar.setOnSeekBarChangeListener(listener);

        refreshColorPicker(hsv, swatch, hex_label, hue_bar, sat_bar, val_bar);

        new AlertDialog.Builder(this)
                .setTitle(Lang.get("editor.color_title"))
                .setView(root)
                .setPositiveButton(Lang.get("common.ok"), (dialog, which) -> {
                    target.setText(hexOf(Color.HSVToColor(hsv)));
                    renderTypeColorPreview();
                })
                .setNegativeButton(Lang.get("common.cancel"), null)
                .show();
    }

    private void refreshColorPicker(float[] hsv, View swatch, TextView hex_label, SeekBar hue_bar, SeekBar sat_bar, SeekBar val_bar) {

        hsv[0] = hue_bar.getProgress();
        hsv[1] = sat_bar.getProgress() / 100f;
        // 明度不给 0 否则整块全黑 看不出色相
        hsv[2] = Math.max(val_bar.getProgress(), 2) / 100f;

        int color = Color.HSVToColor(hsv);

        swatch.setBackground(rounded(color, 12, COLOR_BORDER, 1));
        hex_label.setText(hexOf(color));

        // 色相条：整条彩虹
        hue_bar.setProgressDrawable(new GradientDrawable(
                GradientDrawable.Orientation.LEFT_RIGHT,
                new int[]{
                        0xFFFF0000, 0xFFFFFF00, 0xFF00FF00, 0xFF00FFFF,
                        0xFF0000FF, 0xFFFF00FF, 0xFFFF0000}));

        // 饱和度条：灰 -> 当前色相
        sat_bar.setProgressDrawable(new GradientDrawable(
                GradientDrawable.Orientation.LEFT_RIGHT,
                new int[]{0xFFDDDDDD, Color.HSVToColor(new float[]{hsv[0], 1f, hsv[2]})}));

        // 明度条：黑 -> 当前色
        val_bar.setProgressDrawable(new GradientDrawable(
                GradientDrawable.Orientation.LEFT_RIGHT,
                new int[]{0xFF000000, Color.HSVToColor(new float[]{hsv[0], hsv[1], 1f})}));
    }

    private TextView labelFor(String text) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextSize(11);
        view.setTextColor(COLOR_MUTED);
        view.setPadding(0, dp(6), 0, 0);
        return view;
    }

    private int parseColorOrDefault(String value) {
        if (value != null && !value.isEmpty()) {
            try {
                return Color.parseColor(value);
            } catch (IllegalArgumentException ignored) {
                // 交给下面的默认色
            }
        }

        return COLOR_ACCENT;
    }

    // 底部「添加」：回填过就保存修改 没回填就新建
    private void submitEditor() {
        if (editor_mode == EDITOR_MODE_TYPE) {
            submitTypeForm();
        } else {
            submitTagForm();
        }
    }

    // 添加：
    // - 名称不存在 -> 新建类型
    // - 名称已存在 -> 只改它的颜色
    // 添加后不关闭界面 方便连续添加
    private void submitTypeForm() {
        final String name = edit_type_name.getText().toString().trim();
        final String color = edit_type_color.getText().toString().trim();

        if (name.isEmpty()) {
            toast(Lang.get("editor.name_required"));
            return;
        }

        if (!TagLibrary.isValidHexColor(color)) {
            toast(Lang.get("editor.color_invalid"));
            return;
        }

        runAction(Lang.get("editor.add_type_action"), () -> {
            boolean ok;
            String note;

            if (serve.getTypeTag().containsKey(name)) {
                ok = serve.setTypeColor(name, color);
                note = Lang.get("editor.color_updated_prefix") + name;
            } else {
                ok = serve.addType(name, color);
                note = Lang.get("editor.type_added_prefix") + name;
            }

            if (ok) {
                serve.saveTag();
            }

            toast(ok ? note : Lang.get("common.add_failed") + serve.getLastError());

            if (ok) {
                runOnUiThread(() -> {
                    editor_selected_type = null;
                    edit_type_name.setText("");
                    edit_type_color.setText(DEFAULT_TAG_COLOR);
                    renderEditorTypeList();
                    renderTypeColorPreview();
                });
            }
        });
    }

    // 标签同理：不存在就新建 已存在只改所属类型 不重命名
    private void submitTagForm() {
        final String name = edit_tag_name.getText().toString().trim();
        final String group = editor_selected_type;

        if (name.isEmpty()) {
            toast(Lang.get("editor.tag_name_required"));
            return;
        }

        if (group == null) {
            toast(Lang.get("editor.pick_group_first"));
            return;
        }

        runAction(Lang.get("editor.add_tag_action"), () -> {
            boolean ok;
            String note;

            if (serve.hasTag(name)) {
                ok = serve.setTagType(name, group);
                note = Lang.get("editor.tag_moved_prefix") + name + Lang.get("editor.tag_moved_middle") + group + "」";
            } else {
                ok = serve.addTag(name, group);
                note = Lang.get("editor.tag_added_prefix") + name;
            }

            if (ok) {
                serve.saveTag();
            }

            toast(ok ? note : Lang.get("common.add_failed") + serve.getLastError());

            if (ok) {
                runOnUiThread(() -> {
                    editor_selected_tag = null;
                    edit_tag_name.setText("");
                    renderEditorTagList();
                });
            }
        });
    }

    // 底部「删除」：删掉列表里选中的那个
    private void deleteEditorTarget() {
        if (editor_mode == EDITOR_MODE_TYPE) {
            deleteEditorType();
        } else {
            deleteEditorTag();
        }
    }

    private void deleteEditorType() {
        final String type = editor_selected_type;

        if (type == null) {
            toast(Lang.get("editor.delete_type_hint"));
            return;
        }

        new AlertDialog.Builder(this)
                .setTitle(Lang.get("editor.delete_type_action"))
                .setMessage(Lang.f("editor.delete_type_message", type))
                .setPositiveButton(Lang.get("common.delete"), (dialog, which) -> runAction(Lang.get("editor.delete_type_action"), () -> {
                    boolean ok = serve.removeType(type);

                    if (ok) {
                        serve.saveTag();
                    }

                    toast(ok ? Lang.get("editor.type_deleted_prefix") + type : Lang.get("common.delete_failed") + serve.getLastError());

                    if (ok) {
                        runOnUiThread(() -> {
                            editor_selected_type = null;
                            edit_type_name.setText("");
                            edit_type_color.setText(DEFAULT_TAG_COLOR);
                            renderEditorTypeList();
                            renderTypeColorPreview();
                        });
                    }
                }))
                .setNegativeButton(Lang.get("common.cancel"), null)
                .show();
    }

    private void deleteEditorTag() {
        final String tag = editor_selected_tag;

        if (tag == null) {
            toast(Lang.get("editor.delete_tag_hint"));
            return;
        }

        new AlertDialog.Builder(this)
                .setTitle(Lang.get("editor.delete_tag_action"))
                .setMessage(Lang.f("editor.delete_tag_message", tag))
                .setPositiveButton(Lang.get("common.delete"), (dialog, which) -> runAction(Lang.get("editor.delete_tag_action"), () -> {
                    boolean ok = serve.removeTag(tag);

                    if (ok) {
                        serve.saveTag();
                    }

                    toast(ok ? Lang.get("editor.tag_deleted_prefix") + tag : Lang.get("common.delete_failed") + serve.getLastError());

                    if (ok) {
                        runOnUiThread(() -> {
                            editor_selected_tag = null;
                            edit_tag_name.setText("");
                            renderEditorTagList();
                        });
                    }
                }))
                .setNegativeButton(Lang.get("common.cancel"), null)
                .show();
    }

    // 给文件添加 / 删除标签

    private void showFileTagOverlay(final FileDatabase.FileInfo info) {
        file_tag_target = info;
        file_tag_working.clear();

        if (info.tags != null) {
            file_tag_working.addAll(info.tags);
        }

        overlay_host.removeAllViews();
        file_tag_overlay = getLayoutInflater().inflate(R.layout.overlay_file_tags, overlay_host, false);
        overlay_host.addView(file_tag_overlay);
        overlay_host.setVisibility(View.VISIBLE);

        file_tag_current = file_tag_overlay.findViewById(R.id.fileTagCurrent);
        file_tag_library = file_tag_overlay.findViewById(R.id.fileTagLibrary);

        ImageView icon = file_tag_overlay.findViewById(R.id.fileTagIcon);
        bindFileIcon(icon, info, dp(10));

        if (wantsThumbnail(info, 1)) {
            loadThumbnail(icon, info);
        }

        ((TextView) file_tag_overlay.findViewById(R.id.fileTagName))
                .setText(info.file_ref.isRoot() ? "(root)" : info.file_ref.getName());

        String relative = info.file_ref.getRelativePath();
        ((TextView) file_tag_overlay.findViewById(R.id.fileTagPath))
                .setText(relative.isEmpty()
                        ? info.file_ref.getRootId()
                        : info.file_ref.getRootId() + " / " + relative);

        file_tag_overlay.findViewById(R.id.btnFileTagCancel).setOnClickListener(v -> hideOverlay());
        file_tag_overlay.findViewById(R.id.btnFileTagSave).setOnClickListener(v -> saveFileTags());

        applyOverlayTexts();
        renderFileTagCurrent();
        renderLibraryInto(file_tag_library, 0);
    }

    // 当前标签：点标签即从文件上移除
    private void renderFileTagCurrent() {
        file_tag_current.removeAllViews();

        if (file_tag_working.isEmpty()) {
            file_tag_current.addView(buildHint(Lang.get("editor.tag_list_empty")));
            return;
        }

        for (final String tag : new ArrayList<>(file_tag_working)) {
            file_tag_current.addView(buildChip(tag, colorOfTag(tag), true, () -> {
                file_tag_working.remove(tag);
                renderFileTagCurrent();
                renderLibraryInto(file_tag_library, 0);
            }));
        }
    }

    private void saveFileTags() {
        final FileDatabase.FileInfo info = file_tag_target;

        if (info == null) {
            hideOverlay();
            return;
        }

        final List<String> current = info.tags == null ? new ArrayList<>() : new ArrayList<>(info.tags);
        final List<String> toAdd = new ArrayList<>();
        final List<String> toRemove = new ArrayList<>();

        for (String tag : file_tag_working) {
            if (!current.contains(tag)) {
                toAdd.add(tag);
            }
        }

        for (String tag : current) {
            if (!file_tag_working.contains(tag)) {
                toRemove.add(tag);
            }
        }

        hideOverlay();

        runAction(Lang.get("ftag.save_action"), () -> {
            boolean ok = true;

            for (String tag : toAdd) {
                ok &= serve.addFileTag(info.file_ref, tag);
            }

            if (!toRemove.isEmpty()) {
                ok &= serve.removeFileTag(info.file_ref, toRemove);
            }

            if (ok) {
                // 原版流程：先改真实文件 再刷新数据库
                // Filename 模式改过名字时 TagServe 会自己认清新路径
                ok = serve.updateFile(info.file_ref);
            }

            FileRef written = serve.getLastWrittenPath();
            String note = written != null && !written.equals(info.file_ref)
                    ? Lang.get("ftag.filename_changed_prefix") + written.getRelativePath() : "";

            toast(ok ? Lang.get("ftag.updated") + note : Lang.get("common.update_failed") + serve.getLastError());
        });
    }

    // 设置 / 菜单 / 帮助

    private void renderSettings() {
        if (general_group == null) {
            return;
        }

        general_group.removeAllViews();
        data_group.removeAllViews();
        support_group.removeAllViews();

        // 常规
        // 点整行就在语言列表里轮换（只有具体语言 没有「跟随系统」）
        addRow(general_group, buildRow(R.drawable.globe, Lang.get("settings.language"),
                language_code + "  ·  " + Lang.currentName() + Lang.get("settings.language_hint"),
                buildValue(language_code + " ›"),
                this::cycleLanguage));

        addRow(general_group, buildThemeRow());

        // 数据与工具
        addRow(data_group, buildRow(R.drawable.database, Lang.get("settings.status"), status_text, null, null));

        String mode = serve == null
                ? "…"
                : (serve.getDefaultMode() == StoreMode.SIDECAR ? "Sidecar" : "Filename");

        addRow(data_group, buildRow(R.drawable.arrow_right_left, Lang.get("settings.mode"),
                Lang.get("settings.mode_current_prefix") + mode + Lang.get("settings.mode_hint"),
                buildValue(mode + " ›"),
                this::showConvertModeDialog));

        // 与「模式转换」一致：整行可点 右侧只留箭头 不再放黑色小按钮
        addRow(data_group, buildRow(R.drawable.refresh_cw, Lang.get("settings.refresh_index"), Lang.get("settings.refresh_desc"),
                buildValue("›"),
                () -> runAction(Lang.get("dir.action_refresh"), () -> {
                    boolean ok = serve.refreshAll();
                    toast(ok ? Lang.get("dir.refresh_done") : Lang.get("common.refresh_failed") + serve.getLastError());
                })));

        addRow(data_group, buildRow(R.drawable.brush_cleaning, Lang.get("settings.cleanup"), Lang.get("settings.cleanup_desc"),
                buildValue("›"),
                () -> runAction(Lang.get("settings.cleanup_running"), () -> {
                    boolean ok = serve.getFileDatabase().cleanupInvalid();
                    ok &= serve.getFileDatabase().clearRepeat();
                    toast(ok ? Lang.get("settings.cleanup_done") : Lang.get("common.cleanup_failed") + serve.getFileDatabase().getLastError());
                })));

        addRow(data_group, buildRow(R.drawable.file_up, Lang.get("settings.export"), Lang.get("settings.export_desc"),
                buildValue("›"), this::exportTags));

        addRow(data_group, buildRow(R.drawable.file_down, Lang.get("settings.import"), Lang.get("settings.import_desc"),
                buildValue("›"), this::importTags));

        // 存储方式固定成「所有文件访问 + 应用内选目录」了 保留这一行给用户看
        addRow(data_group, buildRow(R.drawable.database, Lang.get("settings.storage_mode"),
                storageModeLabel(),
                buildValue("›"),
                this::showStorageModeInfo));

        // 支持与关于
        addRow(support_group, buildRow(R.drawable.circle_question_mark, Lang.get("settings.help"), Lang.get("settings.help_desc"),
                buildValue("›"), this::showHelp));

        addRow(support_group, buildRow(R.drawable.info, Lang.get("settings.about"), Lang.get("settings.about_desc"),
                buildValue("›"), this::openProjectPage));

        addRow(support_group, buildRow(R.drawable.wrench, Lang.get("settings.advanced"), Lang.get("settings.advanced_desc"),
                buildBadge(Lang.get("common.reserved")), null));
    }

    // 设置页里显示的存储方式（只有这一种）
    private String storageModeLabel() {
        return hasAllFilesAccess()
                ? Lang.get("settings.storage_all")
                : Lang.get("settings.storage_all_denied");
    }

    private void addRow(LinearLayout group, View row) {
        if (group.getChildCount() > 0) {
            View divider = new View(this);
            divider.setLayoutParams(new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, dp(1)));
            divider.setBackgroundColor(0xFFF0F2F4);
            group.addView(divider);
        }

        group.addView(row);
    }

    private View buildRow(int icon_res, String label, String desc, View right, final Runnable on_click) {

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(14), dp(11), dp(14), dp(11));

        // 图标统一用矢量图 颜色靠 tint 跟正文一致
        ImageView iconView = new ImageView(this);
        iconView.setImageResource(icon_res);
        iconView.setImageTintList(ColorStateList.valueOf(COLOR_TEXT));
        iconView.setScaleType(ImageView.ScaleType.FIT_CENTER);
        iconView.setPadding(dp(7), dp(7), dp(7), dp(7));
        iconView.setBackground(rounded(COLOR_ACTIVE_BG, 9, 0, 0));
        iconView.setLayoutParams(new LinearLayout.LayoutParams(dp(32), dp(32)));
        row.addView(iconView);

        LinearLayout texts = new LinearLayout(this);
        texts.setOrientation(LinearLayout.VERTICAL);

        LinearLayout.LayoutParams textParams = new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        textParams.setMargins(dp(10), 0, dp(8), 0);
        texts.setLayoutParams(textParams);

        TextView labelView = new TextView(this);
        labelView.setText(label);
        labelView.setTextSize(13);
        labelView.setTextColor(COLOR_TEXT);

        TextView descView = new TextView(this);
        descView.setText(desc);
        descView.setTextSize(10);
        descView.setTextColor(COLOR_MUTED);
        descView.setSingleLine(true);
        descView.setEllipsize(TextUtils.TruncateAt.MIDDLE);

        texts.addView(labelView);
        texts.addView(descView);
        row.addView(texts);

        if (right != null) {
            row.addView(right);
        }

        if (on_click != null) {
            row.setClickable(true);
            row.setBackgroundResource(android.R.drawable.list_selector_background);
            row.setOnClickListener(v -> on_click.run());
        }

        return row;
    }

    // 主题：占位行
    private View buildThemeRow() {
        return buildRow(R.drawable.palette, Lang.get("settings.theme"),
                Lang.get("settings.theme_placeholder"),
                buildBadge(Lang.get("common.reserved")), null);
    }

    private View buildValue(String text) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextSize(12);
        view.setTextColor(COLOR_MUTED);
        return view;
    }

    private View buildBadge(String text) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextSize(9);
        view.setTextColor(COLOR_MUTED);
        view.setPadding(dp(6), dp(2), dp(6), dp(2));
        view.setBackground(rounded(COLOR_ACTIVE_BG, 6, COLOR_BORDER, 1));
        return view;
    }

    private static String hexOf(int color) {
        return String.format("#%06X", color & 0xFFFFFF);
    }

    // 语言切换：重新载入字典后就地重画 不重启程序
    // 点一下「语言」这一行就切到下一种语言（列表里只有具体语言）
    private void cycleLanguage() {
        List<String> codes = Lang.languages();

        if (codes.isEmpty()) {
            return;
        }

        int index = codes.indexOf(language_code);
        applyLanguage(codes.get(index < 0 ? 0 : (index + 1) % codes.size()));
    }

    private void applyLanguage(String code) {
        if (!Lang.select(code)) {
            toast(Lang.get("common.set_failed") + Lang.lastError());
            return;
        }

        language_code = code;
        prefs.edit().putString(KEY_LANGUAGE, code).apply();

        // 就地刷新所有文案
        applyStaticTexts();
        applyNavTexts();
        applyOverlayTexts();
        renderSearchChips();
        renderSettings();
        renderTagLibrary();
        renderDirectories();

        // 编辑器子界面没打开时那几个容器还是 null 不能直接画
        if (tag_editor_overlay != null) {
            renderEditorTypeList();
            renderEditorTypeOptions();
            renderEditorTagList();
        }

        reload();

        toast(Lang.get("settings.language_saved_prefix") + Lang.currentName());
    }

    private void applyNavTexts() {
        if (nav_labels == null) {
            return;
        }

        for (int i = 0; i < nav_labels.length && i < TAB_LABEL_KEYS.length; i++) {
            nav_labels[i].setText(Lang.get(TAB_LABEL_KEYS[i]));
        }
    }

    // 布局里的静态文案
    // XML 里的中文只是默认值（方便布局预览）实际显示一律查语言表
    private void applyStaticTexts() {
        // 卡片标题前面挂个小图标（矢量图）
        setTitleIcon(R.id.tvFilesTitle, R.drawable.text_align_justify);
        setTitleIcon(R.id.tvDirTitle, R.drawable.folder);
        setTitleIcon(R.id.tvTagTitle, R.drawable.tag);
        setTitleIcon(R.id.tvSyncTitle, R.drawable.arrow_right_left);

        // 顶栏统计用矢量图标（原来的 ▧ / ◈ 是文字符号）
        setCompoundIcon(findViewById(R.id.tvStatFiles), R.drawable.file, COLOR_MUTED, 13);
        setCompoundIcon(findViewById(R.id.tvStatTags), R.drawable.tag, COLOR_MUTED, 13);

        setText(R.id.tvFilterTitle, "browse.filter_title");
        setText(R.id.btnUp, "browse.up");
        setText(R.id.tvDirTitle, "dir.title");
        setText(R.id.btnAddRoot, "dir.add_button");
        setText(R.id.btnManageRoots, "dir.manage_button");
        setText(R.id.tvDirEmpty, "dir.empty_hint");
        setText(R.id.tvTagTitle, "tag.library_title");
        setText(R.id.btnNewType, "tag.new_group");
        setText(R.id.btnNewTag, "tag.new_tag");
        setText(R.id.tvTypeEmpty, "tag.library_empty_action");
        setText(R.id.tvGroupGeneral, "settings.group_general");
        setText(R.id.tvGroupData, "settings.group_data");
        setText(R.id.tvGroupSupport, "settings.group_support");
        setText(R.id.btnSaveSettings, "settings.save");
        setText(R.id.tvSyncTitle, "nav.sync");
        setText(R.id.tvSyncWip, "sync.wip");
    }

    // 图标统一走矢量图 + tint：颜色跟着文字走
    private void setIconTint(ImageView view, int color) {
        view.setImageTintList(ColorStateList.valueOf(color));
    }

    // 给带文字的按钮/标题挂一个矢量图标（颜色自己指定 尺寸按 dp）
    private void setCompoundIcon(TextView view, int icon_res, int color, int size_dp) {
        Drawable icon = ContextCompat.getDrawable(this, icon_res);

        if (view == null || icon == null) {
            return;
        }

        icon.setTint(color);
        icon.setBounds(0, 0, dp(size_dp), dp(size_dp));
        view.setCompoundDrawablePadding(dp(6));
        view.setCompoundDrawablesRelative(icon, null, null, null);
    }

    // 卡片标题前面的小图标
    private void setTitleIcon(int view_id, int icon_res) {
        TextView view = findViewById(view_id);
        Drawable icon = ContextCompat.getDrawable(this, icon_res);

        if (view == null || icon == null) {
            return;
        }

        icon.setTint(COLOR_TEXT);
        icon.setBounds(0, 0, dp(15), dp(15));
        view.setCompoundDrawablePadding(dp(6));
        view.setCompoundDrawablesRelative(icon, null, null, null);
    }

    // 子界面里的静态文案（子界面每次打开都会重新 inflate 所以每次都要刷一遍）
    private void applyOverlayTexts() {
        if (file_tag_overlay != null) {
            setCompoundIcon((TextView) file_tag_overlay.findViewById(R.id.tvFileTagCurrentTitle), R.drawable.tag_x, COLOR_TEXT, 14);
            setCompoundIcon((TextView) file_tag_overlay.findViewById(R.id.tvFileTagLibraryTitle), R.drawable.tag, COLOR_TEXT, 14);

            setText(file_tag_overlay, R.id.btnFileTagCancel, "common.cancel");
            setText(file_tag_overlay, R.id.tvFileTagTitle, "ftag.title");
            setText(file_tag_overlay, R.id.btnFileTagSave, "editor.save");
            setText(file_tag_overlay, R.id.tvFileTagCurrentTitle, "ftag.current_title");
            setText(file_tag_overlay, R.id.tvFileTagLibraryTitle, "filter.pick_from_library");
            setText(file_tag_overlay, R.id.tvFileTagLibraryHint, "ftag.library_hint");
        }

        if (search_tag_overlay != null) {
            setText(search_tag_overlay, R.id.btnSearchTagCancel, "common.cancel");
            setText(search_tag_overlay, R.id.tvSearchTagTitle, "filter.picker_title");
            setText(search_tag_overlay, R.id.btnSearchTagConfirm, "common.confirm");
            setText(search_tag_overlay, R.id.tvSearchTagHint, "filter.picker_hint");
            setText(search_tag_overlay, R.id.tvSearchTagLibraryTitle, "filter.pick_from_library");
        }

        if (dir_browser_overlay != null) {
            setText(dir_browser_overlay, R.id.btnDirBrowserBack, "common.back");
            setText(dir_browser_overlay, R.id.tvDirBrowserTitle, "dir.browser_title");
            setText(dir_browser_overlay, R.id.btnDirBrowserUp, "dir.browser_up");
            setText(dir_browser_overlay, R.id.btnDirBrowserNew, "dir.browser_new_folder");
            setText(dir_browser_overlay, R.id.btnDirBrowserChoose, "dir.browser_choose");
            setText(dir_browser_overlay, R.id.tvDirBrowserEmpty, "dir.browser_empty");
        }

        if (dir_manage_overlay != null) {
            setText(dir_manage_overlay, R.id.btnDirManageBack, "common.back");
            setText(dir_manage_overlay, R.id.tvDirManageTitle, "dir.manage_title");
            setText(dir_manage_overlay, R.id.tvDirManageEmpty, "dir.manage_empty");
            setText(dir_manage_overlay, R.id.btnDirManageOpen, "dir.open");
            setText(dir_manage_overlay, R.id.btnDirManageRefresh, "common.refresh");
            setText(dir_manage_overlay, R.id.btnDirManageDelete, "common.delete");
        }

        if (tag_editor_overlay != null) {
            setText(tag_editor_overlay, R.id.btnEditorBack, "common.back");
            setText(tag_editor_overlay, R.id.segType, "editor.title_type");
            setText(tag_editor_overlay, R.id.segTag, "editor.title_tag");
            setText(tag_editor_overlay, R.id.btnEditorDelete, "editor.delete");
            setText(tag_editor_overlay, R.id.btnEditorAdd, "editor.add");
            setText(tag_editor_overlay, R.id.tvTypeNameLabel, "editor.type_name_label");
            setText(tag_editor_overlay, R.id.tvTypeListHint, "editor.type_list_hint");
            setText(tag_editor_overlay, R.id.tvColorLabel, "editor.color_label");
            setText(tag_editor_overlay, R.id.btnPickColor, "editor.pick_color");
            setText(tag_editor_overlay, R.id.tvColorHint, "editor.color_hint");
            setText(tag_editor_overlay, R.id.tvPreviewLabel, "editor.preview_label");
            setText(tag_editor_overlay, R.id.tvTypeListTitle, "editor.type_list_title");
            setText(tag_editor_overlay, R.id.tvGroupLabel, "editor.group_label");
            setText(tag_editor_overlay, R.id.tvTagNameLabel, "editor.tag_name_label");
            setText(tag_editor_overlay, R.id.tvTagNameNote, "editor.tag_name_note");
            setText(tag_editor_overlay, R.id.tvTagListTitle, "editor.tag_list_title");
            setHint(tag_editor_overlay, R.id.editTypeName, "editor.type_name_hint");
            setHint(tag_editor_overlay, R.id.editTagName, "editor.tag_name_hint");
            setHint(tag_editor_overlay, R.id.editTypeNewName, "editor.new_type_name_hint");
            setText(tag_editor_overlay, R.id.btnEditorRename, "common.rename");
        }
    }

    private void setText(int view_id, String key) {
        setText(findViewById(view_id), view_id, key);
    }

    private void setText(View root, int view_id, String key) {
        if (root == null) {
            return;
        }

        TextView view = root.findViewById(view_id);

        if (view != null) {
            view.setText(Lang.get(key));
        }
    }

    private void setHint(View root, int view_id, String key) {
        if (root == null) {
            return;
        }

        TextView view = root.findViewById(view_id);

        if (view != null) {
            view.setHint(Lang.get(key));
        }
    }

    // 关于 TagMeow：直接用系统浏览器打开项目主页
    private void openProjectPage() {
        try {
            startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(PROJECT_URL)));
        } catch (ActivityNotFoundException error) {
            toast(Lang.get("about.no_browser"));
        }
    }

    // 导出 / 导入标签库

    // 导出：每次都走系统「保存到…」对话框
    // 默认导出目录那套要 SAF tree 授权 跟着系统选择器一起砍了
    private void exportTags() {
        export_picker.launch(EXPORT_FILE_NAME);
    }

    private void onExportPicked(Uri uri) {
        if (uri == null) {
            return;
        }

        runAction(Lang.get("settings.export"), () -> {
            byte[] data = readTagLibraryBytes();

            if (data == null) {
                toast(Lang.get("export.read_failed"));
                return;
            }

            try (OutputStream out = getContentResolver().openOutputStream(uri, "wt")) {
                if (out == null) {
                    toast(Lang.get("export.no_target"));
                    return;
                }

                out.write(data);
                out.flush();
                toast(Lang.get("export.done"));
            } catch (IOException error) {
                toast(Lang.get("common.export_failed") + error.getMessage());
            }
        });
    }

    private void importTags() {
        import_picker.launch(new String[]{"application/json", "*/*"});
    }

    private void onImportPicked(Uri uri) {
        if (uri == null) {
            return;
        }

        runAction(Lang.get("settings.import"), () -> {
            byte[] data;

            try (InputStream in = getContentResolver().openInputStream(uri)) {
                if (in == null) {
                    toast(Lang.get("import.no_source"));
                    return;
                }

                ByteArrayOutputStream buffer = new ByteArrayOutputStream();
                byte[] chunk = new byte[8192];
                int read;

                while ((read = in.read(chunk)) > 0) {
                    buffer.write(chunk, 0, read);
                }

                data = buffer.toByteArray();
            } catch (IOException error) {
                toast(Lang.get("common.read_failed") + error.getMessage());
                return;
            }

            // 先落到临时文件 再交给标签库做「合并」（TagLibrary.mergeTags）：
            // 本类为空才整体替换 否则只补本类没有的类型和标签 不覆盖已有内容
            File temp = new File(getCacheDir(), "import-tags.json");

            try (OutputStream out = new FileOutputStream(temp)) {
                out.write(data);
                out.flush();
            } catch (IOException error) {
                toast(Lang.get("common.write_failed") + error.getMessage());
                return;
            }

            boolean ok = serve.mergeTag(temp) && serve.saveTag();
            temp.delete();

            toast(ok ? Lang.get("import.done") : Lang.get("common.import_failed") + serve.getLastError());
        });
    }

    private byte[] readTagLibraryBytes() {
        File source = serve.getTagLibrary().getConfigFile();

        try (InputStream in = new java.io.FileInputStream(source)) {
            ByteArrayOutputStream buffer = new ByteArrayOutputStream();
            byte[] chunk = new byte[8192];
            int read;

            while ((read = in.read(chunk)) > 0) {
                buffer.write(chunk, 0, read);
            }

            return buffer.toByteArray();
        } catch (IOException error) {
            return null;
        }
    }

    private void showConvertModeDialog() {
        if (serve == null) {
            return;
        }

        final StoreMode from = serve.getDefaultMode();
        final StoreMode to = from == StoreMode.SIDECAR ? StoreMode.FILENAME : StoreMode.SIDECAR;

        String message = Lang.f("convert.message", name(from), name(to));

        new AlertDialog.Builder(this)
                .setTitle(Lang.get("convert.title"))
                .setMessage(message)
                .setPositiveButton(Lang.get("convert.start"), (dialog, which) -> runAction(Lang.get("convert.action"), () -> {
                    long start = System.currentTimeMillis();
                    boolean ok = serve.convertMode(from, to);
                    long cost = System.currentTimeMillis() - start;

                    saveDefaultMode(serve.getDefaultMode());

                    toast(ok ? Lang.get("convert.done_prefix") + cost + "ms" : Lang.get("common.convert_failed") + serve.getLastError());
                }))
                .setNegativeButton(Lang.get("common.cancel"), null)
                .show();
    }

    private void showHelp() {
        // 帮助文案也走语言表 不要写死在代码里
        new AlertDialog.Builder(this)
                .setTitle(Lang.get("settings.help"))
                .setMessage(Lang.get("help.body")
                        + "\n\n──────────\n\n"
                        + Lang.get("about.body"))
                .setPositiveButton(Lang.get("common.know"), null)
                .show();
    }


    // 基础设施
    // 在 worker 上执行模块操作 完成后自动重画
    private void runAction(String label, Runnable task) {
        worker.execute(() -> {
            try {
                if (serve == null) {
                    toast(label + Lang.get("run.engine_not_ready"));
                    return;
                }

                task.run();
            } catch (Throwable error) {
                toast(label + Lang.get("run.error_suffix") + error);
            }

            reload();
        });
    }

    private void toast(final String message) {
        // 提示也写一份到 logcat：出问题时能直接看到失败原因
        Log.d(TAG, message);

        runOnUiThread(() -> Toast.makeText(this, message, Toast.LENGTH_SHORT).show());
    }

    private static String name(StoreMode mode) {
        return mode == StoreMode.SIDECAR ? "Sidecar" : "Filename";
    }

    private StoreMode readDefaultMode() {
        String value = prefs.getString(KEY_DEFAULT_MODE, null);
        return "FILENAME".equals(value) ? StoreMode.FILENAME : StoreMode.SIDECAR;
    }

    private void saveDefaultMode(StoreMode mode) {
        prefs.edit().putString(KEY_DEFAULT_MODE, mode.name()).apply();
    }

    // 用系统应用打开文件

    private void openFileWithSystem(final FileDatabase.FileInfo info) {
        final Uri uri = resolveOpenUri(info);

        if (uri == null) {
            toast(Lang.get("open.no_uri"));
            return;
        }

        String mime = mimeTypeOf(info.file_ref.getName());

        Intent intent = new Intent(Intent.ACTION_VIEW);
        intent.setDataAndType(uri, mime);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);

        try {
            startActivity(Intent.createChooser(intent, Lang.get("open.chooser")));
        } catch (ActivityNotFoundException error) {
            toast(Lang.get("open.no_app_prefix") + mime + Lang.get("open.no_app_suffix"));
        } catch (SecurityException error) {
            toast(Lang.get("open.denied"));
        }
    }

    // 打开文件用的 Uri：
    // - SAF：document Uri（content://）直接交给系统应用
    // - 所有文件访问：数据库里存的是绝对路径 必须先过 FileProvider
    //   否则 Uri 没有 scheme 外部应用打不开（Android 7+ 也不许直接传 file://）
    private Uri resolveOpenUri(FileDatabase.FileInfo info) {
        String original = info.original_uri;

        if (original == null || original.isEmpty()) {
            return null;
        }

        Uri parsed = Uri.parse(original);

        if ("file".equals(parsed.getScheme())) {
            return providerUriOf(new File(parsed.getPath()));
        }

        if (parsed.getScheme() == null) {
            // 绝对路径（所有文件访问模式）
            return providerUriOf(new File(original));
        }

        return parsed;
    }

    private Uri providerUriOf(File file) {
        try {
            return FileProvider.getUriForFile(
                    this,
                    getPackageName() + ".fileprovider",
                    file);
        } catch (IllegalArgumentException error) {
            return null;
        }
    }

    private static String mimeTypeOf(String file_name) {
        int dot = file_name.lastIndexOf('.');

        if (dot < 0 || dot == file_name.length() - 1) {
            return "*/*";
        }

        String extension = file_name.substring(dot + 1).toLowerCase();
        String mime = MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension);

        return mime == null ? "*/*" : mime;
    }

    // 以下便于调试与测试观察状态
    boolean isSearchMode() {
        return search_mode;
    }

    FileRef getCurrentDirectory() {
        return current_directory;
    }

    long getIndexedFileCount() {
        return indexed_file_count;
    }

    List<String> getTypeNames() {
        return new ArrayList<>(tag_types.keySet());
    }

    TagServe getTagServe() {
        return serve;
    }
}
