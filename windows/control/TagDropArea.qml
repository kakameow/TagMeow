import QtQuick

// 标签落点 收下"拖过来的标签"并发 tagDropped(tag)

DropArea {
    id: root

    // 收下标签后发一次 标签名非空才发
    signal tagDropped(string tag)

    onDropped: (drop) => {
        drop.accepted = true
        var tag = tagOf(drop)
        if (tag.length > 0)
            root.tagDropped(tag)
    }

    // 取出拖拽里的标签名

    function tagOf(drop) {
        var src = drop.source
        if (src && typeof src.tagText === "string" && src.tagText.length > 0)
            return src.tagText
        if (src && typeof src.text === "string" && src.text.length > 0)
            return String(src.text)
        var text = drop.getDataAsString("text/plain")
        return text ? String(text) : ""
    }
}
