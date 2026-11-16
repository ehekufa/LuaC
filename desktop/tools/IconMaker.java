import java.awt.*;
import java.awt.image.BufferedImage;
import java.io.*;
import javax.imageio.ImageIO;

/** Converts the original LuaC PNG to an ICO without replacing its design. */
public class IconMaker {
    static void u16(DataOutputStream out, int n) throws IOException { out.writeByte(n & 255); out.writeByte((n >>> 8) & 255); }
    static void u32(DataOutputStream out, int n) throws IOException { u16(out, n); u16(out, n >>> 16); }
    public static void main(String[] args) throws Exception {
        BufferedImage source = ImageIO.read(new File(args[0]));
        BufferedImage icon = new BufferedImage(256, 256, BufferedImage.TYPE_INT_ARGB);
        Graphics2D g = icon.createGraphics(); g.setRenderingHint(RenderingHints.KEY_INTERPOLATION, RenderingHints.VALUE_INTERPOLATION_BICUBIC); g.drawImage(source, 0, 0, 256, 256, null); g.dispose();
        ByteArrayOutputStream png = new ByteArrayOutputStream(); ImageIO.write(icon, "png", png);
        try (DataOutputStream out = new DataOutputStream(new FileOutputStream(args[1]))) {
            u16(out, 0); u16(out, 1); u16(out, 1); out.writeByte(0); out.writeByte(0); out.writeByte(0); out.writeByte(0);
            u16(out, 1); u16(out, 32); u32(out, png.size()); u32(out, 22); png.writeTo(out);
        }
    }
}
