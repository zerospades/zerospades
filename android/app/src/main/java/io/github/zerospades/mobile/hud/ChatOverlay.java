package io.github.zerospades.mobile.hud;

import android.app.Activity;
import android.content.Context;
import android.graphics.Color;
import android.text.InputFilter;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;

import io.github.zerospades.mobile.R;

/** Android chat surface. The host must send messages to the real game. */
public final class ChatOverlay extends LinearLayout {
    public interface Sender { void send(String message, boolean global); }
    private final Activity activity;
    private final Sender sender;
    private final TextView messages;
    private final EditText input;
    private final ScrollView scroll;
    private final Switch teamChat;
    private Runnable onClosed;
    private final StringBuilder history = new StringBuilder();

    public ChatOverlay(Activity activity, Sender sender) {
        super(activity);
        this.activity = activity;
        this.sender = sender;
        setOrientation(VERTICAL);
        setPadding(dp(20), dp(12), dp(20), dp(12));
        setBackgroundColor(0xcc000000); // 80% opaque
        setClickable(true);
        setFocusableInTouchMode(true);

        LinearLayout header = new LinearLayout(activity);
        header.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = new TextView(activity);
        title.setText(R.string.chat_title); title.setTextColor(Color.WHITE); title.setTextSize(20);
        header.addView(title, new LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        teamChat = new Switch(activity);
        teamChat.setText(R.string.chat_team); teamChat.setTextColor(Color.WHITE);
        header.addView(teamChat);
        Button close = new Button(activity); close.setText(R.string.chat_close);
        close.setOnClickListener(new OnClickListener() { @Override public void onClick(View v) { close(); } });
        header.addView(close); addView(header);

        scroll = new ScrollView(activity);
        messages = new TextView(activity); messages.setTextColor(Color.WHITE); messages.setTextSize(16);
        scroll.addView(messages);
        addView(scroll, new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
        LinearLayout composer = new LinearLayout(activity);
        input = new EditText(activity); input.setSingleLine(true); input.setTextColor(Color.WHITE);
        input.setHintTextColor(0xffbcbcbc); input.setHint(R.string.chat_hint);
        input.setFilters(new InputFilter[] { new InputFilter.LengthFilter(200) });
        input.setImeOptions(EditorInfo.IME_ACTION_SEND);
        input.setOnEditorActionListener((view, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_SEND) { send(); return true; }
            return false;
        });
        composer.addView(input, new LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        Button send = new Button(activity); send.setText(R.string.chat_send);
        send.setOnClickListener(new OnClickListener() { @Override public void onClick(View v) { send(); } });
        composer.addView(send); addView(composer);
        setVisibility(GONE);
    }
    public void setOnClosed(Runnable listener) { onClosed = listener; }
    public void open() {
        setVisibility(VISIBLE);
        input.requestFocus();
        input.post(new Runnable() { @Override public void run() {
            if (getVisibility() == VISIBLE) {
                InputMethodManager imm = (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);
                if (imm != null) imm.showSoftInput(input, InputMethodManager.SHOW_IMPLICIT);
            }
        }});
    }
    public void close() {
        InputMethodManager imm = (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);
        if (imm != null) imm.hideSoftInputFromWindow(input.getWindowToken(), 0);
        input.clearFocus(); setVisibility(GONE);
        if (onClosed != null) onClosed.run();
    }
    public boolean isOpen() { return getVisibility() == VISIBLE; }
    public void clearMessages() { history.setLength(0); messages.setText(""); input.setText(""); }
    public void addMessage(String message) {
        if (message == null || message.isEmpty()) return;
        if (history.length() > 0) history.append('\n');
        history.append(message);
        // Bound retained text when connected to a busy server.
        if (history.length() > 16000) {
            int end = history.indexOf("\n", history.length() - 12000);
            history.delete(0, end >= 0 ? end + 1 : history.length() - 12000);
        }
        messages.setText(history.toString());
        scroll.post(new Runnable() { @Override public void run() { scroll.fullScroll(View.FOCUS_DOWN); } });
    }
    private void send() {
        String message = input.getText().toString().trim();
        if (message.isEmpty()) return;
        sender.send(message, !teamChat.isChecked());
        input.setText("");
    }
    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
}
