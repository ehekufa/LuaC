package com.luac.studio;

import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.PopupMenu;
import android.widget.TextView;
import androidx.recyclerview.widget.RecyclerView;
import com.google.android.material.button.MaterialButton;
import java.io.File;
import java.util.List;

final class ProjectAdapter extends RecyclerView.Adapter<ProjectAdapter.Holder> {
    interface Listener {
        void onOpen(File project);
        void onRun(File project);
        void onRename(File project);
        void onExport(File project);
        void onDelete(File project);
    }

    private final ProjectStore store;
    private final Listener listener;
    private List<File> items;

    ProjectAdapter(ProjectStore store, List<File> items, Listener listener) {
        this.store = store;
        this.items = items;
        this.listener = listener;
    }

    void setItems(List<File> items) {
        this.items = items;
        notifyDataSetChanged();
    }

    @Override public Holder onCreateViewHolder(ViewGroup parent, int viewType) {
        View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_project, parent, false);
        return new Holder(view);
    }

    @Override public void onBindViewHolder(Holder holder, int position) {
        File project = items.get(position);
        holder.name.setText(project.getName());
        int count = store.fileCount(project);
        holder.subtitle.setText(count + (count == 1 ? " file" : " files") + " • Lua project");
        holder.card.setOnClickListener(v -> listener.onOpen(project));
        holder.run.setOnClickListener(v -> listener.onRun(project));
        holder.more.setOnClickListener(v -> {
            PopupMenu menu = new PopupMenu(v.getContext(), v);
            menu.inflate(R.menu.menu_project_item);
            menu.setOnMenuItemClickListener(item -> {
                if (item.getItemId() == R.id.action_export_project) listener.onExport(project);
                else if (item.getItemId() == R.id.action_rename) listener.onRename(project);
                else if (item.getItemId() == R.id.action_delete) listener.onDelete(project);
                return true;
            });
            menu.show();
        });
    }

    @Override public int getItemCount() { return items.size(); }

    static final class Holder extends RecyclerView.ViewHolder {
        final View card;
        final TextView name, subtitle;
        final MaterialButton run, more;
        Holder(View view) {
            super(view);
            card = view.findViewById(R.id.card);
            name = view.findViewById(R.id.name);
            subtitle = view.findViewById(R.id.subtitle);
            run = view.findViewById(R.id.run);
            more = view.findViewById(R.id.more);
        }
    }
}
