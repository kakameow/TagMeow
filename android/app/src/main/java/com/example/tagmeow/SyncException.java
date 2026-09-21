package com.example.tagmeow;

import java.io.EOFException;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.net.SocketException;
import java.net.SocketTimeoutException;
import java.nio.file.AccessDeniedException;
import java.nio.file.NoSuchFileException;

// 同步基础层的失败信号

public final class SyncException extends IOException {

    private final SyncError error;

    public SyncException(SyncError error) {
        super(describe(error));
        this.error = error;
    }

    public SyncException(SyncError error, String detail) {
        super(describe(error) + (detail == null || detail.isEmpty() ? "" : ": " + detail));
        this.error = error;
    }

    public SyncException(SyncError error, String detail, Throwable cause) {
        super(describe(error) + (detail == null || detail.isEmpty() ? "" : ": " + detail), cause);
        this.error = error;
    }

    public SyncError getError() {
        return error;
    }

    public static SyncError errorOf(Throwable error) {
        if (error == null) {
            return SyncError.NONE;
        }

        if (error instanceof SyncException) {
            return ((SyncException) error).getError();
        }

        if (error instanceof SocketTimeoutException) {
            return SyncError.TIMED_OUT;
        }

        if (error instanceof NoSuchFileException || error instanceof FileNotFoundException) {
            return SyncError.NO_SUCH_FILE_OR_DIRECTORY;
        }

        if (error instanceof AccessDeniedException) {
            return SyncError.PERMISSION_DENIED;
        }

        if (error instanceof EOFException) {
            return SyncError.EOF;
        }

        if (error instanceof SocketException) {
            // 「Socket closed」多半是上层主动 close() 造成的 归到被取消
            String message = error.getMessage();

            if (message != null && message.toLowerCase().contains("closed")) {
                return SyncError.OPERATION_CANCELED;
            }

            return SyncError.NOT_CONNECTED;
        }

        return SyncError.IO_ERROR;
    }

    private static String describe(SyncError error) {
        return error == null ? "sync error" : error.name();
    }
}
