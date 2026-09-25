from PIL import Image, ImageStat
from pathlib import Path
cur = Image.open(r'C:\Users\sbsjj\Documents\Codex\2026-09-13\new-chat\work\instrument-deploy\slyce-current-window-forced.png').convert('RGB')
ref = Image.open(r'C:\Users\sbsjj\Downloads\ChatGPT Image Sep 16, 2026, 02_37_44 PM.png').convert('RGB')
print('current_size', cur.size)
print('reference_size', ref.size)
print('current_aspect', round(cur.size[0]/cur.size[1], 4))
print('reference_aspect', round(ref.size[0]/ref.size[1], 4))
# Compare dominant bands after resizing current to reference dimensions.
cur_r = cur.resize(ref.size)
# sample key regions in normalized coords: left browser, top hero, wave center, slice cards, keyboard, right strip
regions = {
 'left_browser': (0.03,0.14,0.22,0.75),
 'top_logo': (0.37,0.04,0.63,0.13),
 'waveform': (0.23,0.24,0.77,0.46),
 'slice_cards': (0.23,0.48,0.77,0.72),
 'right_fx': (0.78,0.14,0.97,0.75),
 'keyboard': (0.03,0.77,0.84,0.89),
}
for name, box in regions.items():
    rb = tuple(int(v*s) for v,s in zip(box, (ref.size[0],ref.size[1],ref.size[0],ref.size[1])))
    c = cur_r.crop(rb); r = ref.crop(rb)
    diff = Image.new('RGB', c.size)
    # mean absolute RGB distance
    cp=list(c.getdata()); rp=list(r.getdata())
    mad=sum((abs(a[0]-b[0])+abs(a[1]-b[1])+abs(a[2]-b[2]))/3 for a,b in zip(cp,rp))/len(cp)
    print(name, 'mean_abs_rgb_diff', round(mad,2), 'cur_mean', tuple(round(x,1) for x in ImageStat.Stat(c).mean), 'ref_mean', tuple(round(x,1) for x in ImageStat.Stat(r).mean))
