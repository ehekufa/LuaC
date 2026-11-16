package com.luac.desktop;

import java.awt.*;
import java.awt.event.*;
import java.awt.image.BufferedImage;
import javax.swing.*;

/** 512px transparent PNG canvas; checkerboard is preview only. */
final class PaintCanvas extends JComponent {
    final BufferedImage image = new BufferedImage(512, 512, BufferedImage.TYPE_INT_ARGB);
    private BufferedImage undo;
    private int lastX, lastY;
    int size = 8; Color color = Color.BLACK; boolean eraser, dirty, drawing;
    PaintCanvas() {
        setPreferredSize(new Dimension(400, 400)); setName("paint-canvas");
        MouseAdapter mouse = new MouseAdapter() {
            public void mousePressed(MouseEvent e) { if (!containsPoint(e)) return; snapshot(); drawing = true; lastX = px(e); lastY = py(e); draw(lastX, lastY, lastX, lastY); }
            public void mouseReleased(MouseEvent e) { drawing = false; }
            public void mouseDragged(MouseEvent e) { if (!drawing) return; int x = px(e), y = py(e); draw(lastX, lastY, x, y); lastX = x; lastY = y; }
        };
        addMouseListener(mouse); addMouseMotionListener(mouse);
    }
    private int side() { return Math.max(1, Math.min(getWidth(), getHeight())); }
    private int left() { return (getWidth() - side()) / 2; } private int top() { return (getHeight() - side()) / 2; }
    private boolean containsPoint(MouseEvent e) { return e.getX() >= left() && e.getX() < left()+side() && e.getY() >= top() && e.getY() < top()+side(); }
    private int px(MouseEvent e) { return Math.max(0, Math.min(511, (e.getX()-left())*512/side())); }
    private int py(MouseEvent e) { return Math.max(0, Math.min(511, (e.getY()-top())*512/side())); }
    private void snapshot() { undo = new BufferedImage(512, 512, BufferedImage.TYPE_INT_ARGB); Graphics2D g=undo.createGraphics(); g.drawImage(image, 0, 0, null); g.dispose(); }
    private void draw(int x, int y, int nx, int ny) {
        Graphics2D g=image.createGraphics(); g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
        g.setComposite(eraser ? AlphaComposite.Clear : AlphaComposite.SrcOver); g.setColor(color); g.setStroke(new BasicStroke(size, BasicStroke.CAP_ROUND, BasicStroke.JOIN_ROUND));
        g.drawLine(x, y, nx, ny); g.dispose(); dirty = true; repaint();
    }
    void clear() { snapshot(); Graphics2D g=image.createGraphics(); g.setComposite(AlphaComposite.Clear); g.fillRect(0, 0, 512, 512); g.dispose(); dirty=true; repaint(); }
    void undo() { if (undo != null) { Graphics2D g=image.createGraphics(); g.setComposite(AlphaComposite.Src); g.drawImage(undo, 0, 0, null); g.dispose(); undo=null; dirty=true; repaint(); } }
    @Override protected void paintComponent(Graphics graphics) {
        Graphics2D g=(Graphics2D)graphics.create(); int side=side(), left=left(), top=top();
        for (int y=0;y<16;y++) for (int x=0;x<16;x++) {
            g.setColor((x+y)%2==0 ? new Color(0xd8d8d8) : new Color(0xaaaaaa));
            int x1=left+x*side/16, x2=left+(x+1)*side/16, y1=top+y*side/16, y2=top+(y+1)*side/16;
            g.fillRect(x1,y1,x2-x1,y2-y1);
        }
        g.drawImage(image,left,top,side,side,null); g.dispose();
    }
}
