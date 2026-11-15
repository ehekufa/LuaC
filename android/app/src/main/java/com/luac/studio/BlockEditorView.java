package com.luac.studio;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.text.InputType;
import android.view.View;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import androidx.appcompat.app.AlertDialog;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** Nested, ordered blocks; the document remains the source of truth, not the views. */
final class BlockEditorView extends ScrollView {
    private final BlockProgram program;
    private final boolean ru;
    private final LinearLayout content;
    private final int pad;
    private static final String[] TYPES = {"print", "set", "change", "repeat", "if"};
    private static final String[] OPS = {"==", "~=", "<", ">", "<=", ">="};

    BlockEditorView(Context context, BlockProgram program, boolean ru) {
        super(context); this.program = program; this.ru = ru;
        pad = (int)(8 * getResources().getDisplayMetrics().density + .5f);
        setFillViewport(true);
        content = new LinearLayout(context); content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(pad, pad, pad, pad); addView(content);
        refresh();
    }
    private String tr(String en, String russian) { return ru ? russian : en; }
    private TextView text(String label) {
        TextView view = new TextView(getContext()); view.setText(label); view.setTextColor(Color.WHITE);
        view.setTextSize(15); view.setPadding(pad, pad, pad, pad); return view;
    }
    private MaterialButton button(String label, Runnable action) {
        MaterialButton button = new MaterialButton(getContext(), null, com.google.android.material.R.attr.materialButtonOutlinedStyle);
        button.setText(label); button.setTextColor(Color.WHITE); button.setAllCaps(false);
        button.setOnClickListener(v -> action.run()); return button;
    }
    private void changed() { program.dirty = true; refresh(); }
    private void error(Exception e) { Toast.makeText(getContext(), String.valueOf(e.getMessage()), Toast.LENGTH_LONG).show(); }
    private void refresh() {
        content.removeAllViews();
        TextView info = text(tr("Blocks run before the existing script. Existing code is kept unchanged.",
            "Блоки выполняются перед существующим скриптом. Исходный код сохраняется без изменений."));
        info.setId(R.id.blocks_info); content.addView(info);
        if (program.recovered) content.addView(text(tr("The script changed outside blocks. It is preserved as existing code; new blocks will be added before it.",
            "Скрипт изменён вне блоков. Он сохранён как исходный код; новые блоки будут добавлены перед ним.")));
        try { drawList(content, program.blocks, 0); } catch (JSONException e) { error(e); }
    }
    private void drawList(LinearLayout parent, JSONArray list, int depth) throws JSONException {
        for (int i = 0; i < list.length(); i++) {
            final int index = i;
            JSONObject block = list.getJSONObject(i);
            String type = block.getString("type");
            LinearLayout card = new LinearLayout(getContext()); card.setOrientation(LinearLayout.VERTICAL);
            GradientDrawable background = new GradientDrawable();
            background.setColor(type.equals("repeat") || type.equals("if") ? 0xff31507b : 0xff285b55);
            background.setCornerRadius(pad * 1.5f); card.setBackground(background);
            LinearLayout.LayoutParams cardParams = new LinearLayout.LayoutParams(-1, -2);
            cardParams.setMargins(0, pad / 2, 0, pad / 2); parent.addView(card, cardParams);
            TextView title = text(summary(block)); title.setTypeface(null, Typeface.BOLD); card.addView(title);
            LinearLayout actions = new LinearLayout(getContext()); actions.setOrientation(LinearLayout.HORIZONTAL);
            addAction(actions, tr("Edit", "Изменить"), () -> edit(block, false, list));
            addAction(actions, "↑", () -> move(list, index, index - 1));
            addAction(actions, "↓", () -> move(list, index, index + 1));
            addAction(actions, "×", () -> {
                new MaterialAlertDialogBuilder(getContext()).setMessage(tr("Delete this block and its contents?", "Удалить блок и его содержимое?"))
                    .setPositiveButton(tr("Delete", "Удалить"), (d, w) -> { list.remove(index); changed(); })
                    .setNegativeButton(tr("Cancel", "Отмена"), null).show();
            });
            card.addView(actions);
            if (type.equals("repeat") || type.equals("if")) {
                LinearLayout nested = new LinearLayout(getContext()); nested.setOrientation(LinearLayout.VERTICAL);
                nested.setPadding(pad * 2, 0, pad, pad); card.addView(nested);
                drawList(nested, block.getJSONArray("children"), depth + 1);
            }
        }
        MaterialButton add = button(tr("+ Add block", "+ Добавить блок"), () -> choose(list, depth));
        if (depth == 0) add.setId(R.id.blocks_add);
        parent.addView(add);
    }
    private void addAction(LinearLayout row, String label, Runnable action) {
        MaterialButton b = button(label, action);
        b.setPadding(0, 0, 0, 0);
        row.addView(b, new LinearLayout.LayoutParams(0, -2, label.length() > 1 ? 2 : 1));
    }
    private void move(JSONArray list, int from, int to) {
        if (to < 0 || to >= list.length()) return;
        try { Object a = list.get(from), b = list.get(to); list.put(from, b); list.put(to, a); changed(); }
        catch (JSONException e) { error(e); }
    }
    private String summary(JSONObject b) throws JSONException {
        switch (b.getString("type")) {
            case "print": return tr("Print: ", "Вывести: ") + b.getString("text");
            case "set": return tr("Set ", "Задать ") + b.getString("name") + " = " + b.getString("value");
            case "change": return tr("Change ", "Изменить ") + b.getString("name") + " + " + b.getString("value");
            case "repeat": return tr("Repeat ", "Повторить ") + b.getString("value") + tr(" times", " раз");
            case "if": return tr("If ", "Если ") + b.getString("name") + " " + b.getString("op") + " " + b.getString("value");
            default: throw new IllegalArgumentException("Unknown block");
        }
    }
    private int count(JSONArray blocks) throws JSONException {
        int total = 0;
        for (int i = 0; i < blocks.length(); i++) {
            JSONObject block = blocks.getJSONObject(i); total++;
            if (block.has("children")) total += count(block.getJSONArray("children"));
        }
        return total;
    }
    private void choose(JSONArray list, int depth) {
        try {
            if (depth > 8 || count(program.blocks) >= 200) {
                Toast.makeText(getContext(), tr("Block limit reached", "Достигнут предел блоков"), Toast.LENGTH_LONG).show(); return;
            }
        } catch (JSONException e) { error(e); return; }
        String[] labels = {tr("Print text", "Вывести текст"), tr("Set variable", "Задать переменную"),
            tr("Change variable", "Изменить переменную"), tr("Repeat", "Повторить"), tr("If", "Если")};
        new MaterialAlertDialogBuilder(getContext()).setTitle(tr("Add block", "Добавить блок"))
            .setItems(labels, (dialog, which) -> {
                try {
                    JSONObject b = new JSONObject().put("type", TYPES[which]);
                    if (which == 0) b.put("text", "Hello!");
                    else if (which == 3) b.put("value", "3").put("children", new JSONArray());
                    else {
                        b.put("name", "score").put("value", "0");
                        if (which == 4) b.put("op", "==").put("children", new JSONArray());
                    }
                    edit(b, true, list);
                } catch (JSONException e) { error(e); }
            }).setNegativeButton(tr("Cancel", "Отмена"), null).show();
    }
    private EditText field(LinearLayout form, String hint, String value) {
        EditText input = new EditText(getContext()); input.setSingleLine(true); input.setTextColor(Color.WHITE);
        input.setHintTextColor(0xffbfc7cf); input.setHint(hint); input.setContentDescription(hint);
        input.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        input.setText(value); form.addView(input); return input;
    }
    private void edit(JSONObject block, boolean adding, JSONArray destination) {
        try {
            String type = block.getString("type");
            LinearLayout form = new LinearLayout(getContext()); form.setOrientation(LinearLayout.VERTICAL);
            form.setPadding(pad * 2, 0, pad * 2, 0);
            form.setFocusableInTouchMode(true);
            EditText text = type.equals("print") ? field(form, tr("Text", "Текст"), block.getString("text")) : null;
            EditText name = type.equals("set") || type.equals("change") || type.equals("if") ?
                field(form, tr("Variable name", "Имя переменной"), block.getString("name")) : null;
            EditText value = !type.equals("print") ? field(form, type.equals("repeat") ? tr("Count (0–10000)", "Количество (0–10000)") : tr("Number", "Число"), block.getString("value")) : null;
            final String[] op = {type.equals("if") ? block.getString("op") : "=="};
            if (type.equals("if")) {
                MaterialButton operator = button(tr("Comparison: ", "Сравнение: ") + op[0], () -> {});
                operator.setOnClickListener(v -> new MaterialAlertDialogBuilder(getContext()).setTitle(tr("Comparison", "Сравнение"))
                    .setItems(OPS, (d, index) -> { op[0] = OPS[index]; operator.setText(tr("Comparison: ", "Сравнение: ") + op[0]); }).show());
                form.addView(operator);
            }
            AlertDialog dialog = new MaterialAlertDialogBuilder(getContext()).setTitle(adding ? tr("New block", "Новый блок") : tr("Edit block", "Изменить блок"))
                .setView(form).setPositiveButton(tr("Save", "Сохранить"), null).setNegativeButton(tr("Cancel", "Отмена"), null).create();
            dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
                String n = name == null ? "" : name.getText().toString().trim();
                String val = value == null ? "" : value.getText().toString().trim();
                if (name != null && !BlockProgram.validName(n)) { name.setError(tr("Use a Lua variable name", "Введите имя переменной Lua")); return; }
                if (value != null && !(type.equals("repeat") ? val.matches("[0-9]{1,5}") && Integer.parseInt(val) <= 10000 : BlockProgram.validNumber(val))) {
                    value.setError(tr("Enter a valid number", "Введите корректное число")); return;
                }
                try {
                    if (text != null) block.put("text", text.getText().toString());
                    if (name != null) block.put("name", n);
                    if (value != null) block.put("value", val);
                    if (type.equals("if")) block.put("op", op[0]);
                    if (adding) destination.put(block);
                    changed(); dialog.dismiss();
                } catch (JSONException e) { error(e); }
            }));
            dialog.show();
            form.requestFocus();
            if (dialog.getWindow() != null) dialog.getWindow().setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
        } catch (JSONException e) { error(e); }
    }
}
