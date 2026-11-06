package io.github.zerospades.mobile.hud;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/** Decodes length-prefixed standard UTF-8 records, never interpreting text as markup. */
public final class ChatMessage {
    public final String prefix, name, suffix;
    public final int color;
    private ChatMessage(String prefix, String name, String suffix, int color) {
        this.prefix=prefix; this.name=name; this.suffix=suffix; this.color=0xff000000 | color;
    }
    public static List<ChatMessage> decode(byte[] bytes) {
        List<ChatMessage> result=new ArrayList<>();
        if (bytes == null) return result;
        ByteBuffer in=ByteBuffer.wrap(bytes);
        while (in.remaining() >= 16 && result.size() < 100) {
            int color=in.getInt(), a=in.getInt(), b=in.getInt(), c=in.getInt();
            if (a<0 || a>512 || b<0 || b>1024 || c<0 || c>4096 ||
                    (long)a+b+c>in.remaining()) break;
            byte[] text=new byte[a+b+c]; in.get(text);
            result.add(new ChatMessage(new String(text,0,a,StandardCharsets.UTF_8),
                    new String(text,a,b,StandardCharsets.UTF_8),
                    new String(text,a+b,c,StandardCharsets.UTF_8),color));
        }
        return result;
    }
}
