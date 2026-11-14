package com.luac.studio;

import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.PopupMenu;
import android.widget.TextView;
import androidx.recyclerview.widget.RecyclerView;
import com.google.android.material.button.MaterialButton;
import java.io.File;
import java.util.List;

final class FileAdapter extends RecyclerView.Adapter<FileAdapter.Holder> {
    interface Listener {
        void onOpen(File file);
        void onRename(File file);
        void onDelete(File file);
    }

    private final Listener listener;
    private List<File> items;

    FileAdapter(List<File> items, Listener listener) {
        this.items = items;
        this.listener = listener;
    }

    void setItems(List<File> items) {
        this.items = items;
        notifyDataSetChanged();
    }

    @Override public Holder onCreateViewHolder(ViewGroup parent, int viewType) {
        View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_file, parent, false);
        StudioTheme.apply(view, StudioTheme.color(parent.getContext()));
        return new Holder(view);
    }

    @Override public void onBindViewHolder(Holder holder, int position) {
        File file = items.get(position);
        holder.name.setText(file.getName());
        holder.icon.setImageResource(icon(file));
        holder.subtitle.setText(subtitle(file));
        holder.card.setOnClickListener(v -> listener.onOpen(file));
        holder.more.setOnClickListener(v -> {
            PopupMenu menu = new PopupMenu(v.getContext(), v);
            menu.inflate(R.menu.menu_item);
            menu.setOnMenuItemClickListener(item -> {
                if (item.getItemId() == R.id.action_rename) listener.onRename(file);
                else if (item.getItemId() == R.id.action_delete) listener.onDelete(file);
                return true;
            });
            menu.show();
        });
    }

    @Override public int getItemCount() { return items.size(); }

    private static int icon(File file) {
        if (file.isDirectory()) return R.drawable.ic_folder;
        String name = file.getName().toLowerCase();
        if (name.endsWith(".png") || name.endsWith(".jpg") || name.endsWith(".jpeg") || name.endsWith(".webp")) return R.drawable.ic_image;
        if (name.endsWith(".lua")) return R.drawable.ic_code;
        return R.drawable.ic_file;
    }

    private static String subtitle(File file) {
        if (file.isDirectory()) return "Folder";
        String name = file.getName().toLowerCase();
        if (name.equals("main.lua")) return "Entry point";
        if (name.endsWith(".lua")) return "Lua script";
        if (name.endsWith(".png") || name.endsWith(".jpg") || name.endsWith(".jpeg") || name.endsWith(".webp")) return "Image asset";
        return "File";
    }

    static final class Holder extends RecyclerView.ViewHolder {
        final View card;
        final TextView name, subtitle;
        final ImageView icon;
        final MaterialButton more;
        Holder(View view) {
            super(view);
            card = view.findViewById(R.id.card);
            name = view.findViewById(R.id.name);
            subtitle = view.findViewById(R.id.subtitle);
            icon = view.findViewById(R.id.icon);
            more = view.findViewById(R.id.more);
        }
    }
}
