package com.luac.desktop;

import java.awt.*;
import java.awt.geom.*;
import javax.swing.*;
import javax.swing.border.EmptyBorder;

/** Small Swing components matching the Android Material surface and controls. */
final class Material {
    static final Color BACKGROUND = new Color(0x090D16), SURFACE = new Color(0x121B2B), WHITE = Color.WHITE;
    static Color dark(Color c) {
        float[] hsv = Color.RGBtoHSB(c.getRed(), c.getGreen(), c.getBlue(), null);
        return Color.getHSBColor(hsv[0], hsv[1], Math.min(hsv[2], .34f));
    }
    static class Card extends JPanel {
        private final int radius;
        Card(LayoutManager layout, int radius) { super(layout); this.radius = radius; setOpaque(false); }
        @Override protected void paintComponent(Graphics graphics) {
            Graphics2D g = (Graphics2D) graphics.create();
            g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
            g.setColor(SURFACE); g.fillRoundRect(0, 0, getWidth(), getHeight(), radius, radius); g.dispose();
            super.paintComponent(graphics);
        }
    }
    static class Button extends JButton {
        private final Color fill;
        Button(String text, Color fill, boolean icon) {
            super(text); this.fill = fill;
            setOpaque(false); setContentAreaFilled(false); setBorderPainted(false); setFocusPainted(false);
            setForeground(WHITE); setFocusable(true); setCursor(Cursor.getPredefinedCursor(Cursor.HAND_CURSOR));
            setFont(getFont().deriveFont(icon ? Font.PLAIN : Font.BOLD, icon ? 21f : 14f));
            setBorder(new EmptyBorder(icon ? 4 : 10, icon ? 12 : 20, icon ? 4 : 10, icon ? 12 : 20));
        }
        @Override protected void paintComponent(Graphics graphics) {
            Graphics2D g = (Graphics2D)graphics.create();
            g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
            if (fill != null) { g.setColor(isEnabled() ? fill : fill.darker()); g.fillRoundRect(0, 0, getWidth(), getHeight(), getHeight(), getHeight()); }
            else if (getModel().isRollover()) { g.setColor(new Color(255, 255, 255, 30)); g.fillOval(2, 2, getWidth()-4, getHeight()-4); }
            g.dispose(); super.paintComponent(graphics);
        }
    }
    static class FileIcon implements Icon {
        private final String type;
        FileIcon(String type) { this.type = type; }
        public int getIconWidth() { return 32; } public int getIconHeight() { return 32; }
        public void paintIcon(Component c, Graphics graphics, int x, int y) {
            Graphics2D g = (Graphics2D) graphics.create(); g.translate(x, y);
            g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
            g.setColor(new Color(0x4BDCB5)); g.setStroke(new BasicStroke(2.3f, BasicStroke.CAP_ROUND, BasicStroke.JOIN_ROUND));
            if (type.equals("folder")) {
                Path2D p = new Path2D.Float(); p.moveTo(3, 8); p.lineTo(12, 8); p.lineTo(15, 11); p.lineTo(29, 11); p.lineTo(29, 24); p.lineTo(3, 24); p.closePath(); g.draw(p);
            } else if (type.equals("code")) {
                g.drawLine(11, 10, 5, 16); g.drawLine(5, 16, 11, 22); g.drawLine(21, 10, 27, 16); g.drawLine(27, 16, 21, 22); g.drawLine(18, 8, 14, 24);
            } else if (type.equals("image")) {
                g.drawRoundRect(3, 5, 26, 22, 4, 4); g.drawOval(9, 9, 4, 4); g.drawLine(5, 24, 13, 16); g.drawLine(13, 16, 17, 20); g.drawLine(17, 20, 22, 15); g.drawLine(22, 15, 28, 22);
            } else { g.drawRoundRect(7, 4, 18, 24, 3, 3); g.drawLine(11, 13, 21, 13); g.drawLine(11, 18, 21, 18); }
            g.dispose();
        }
    }
}
