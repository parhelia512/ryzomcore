cgc -entry fxaa_pp fxaa_pp.cg -profile arbfp1 -O3 -fastmath -fastprecision -o fxaa_pp_arbfp1.txt
cgc -entry fxaa_pp fxaa_pp.cg -profile ps_2_x -O3 -fastmath -fastprecision -o fxaa_pp_ps_2_0.txt
cgc -entry fxaa_vp fxaa_vp.cg -profile arbvp1 -fastmath -fastprecision -o fxaa_vp_arbvp1.txt
cgc -entry stereo_debug_pp stereo_debug_pp.cg -profile arbfp1 -O3 -fastmath -fastprecision -o stereo_debug_pp_arbfp1.txt
cgc -entry stereo_debug_pp stereo_debug_pp.cg -profile ps_2_0 -O3 -fastmath -fastprecision -o stereo_debug_pp_ps_2_0.txt
cgc -entry water_fp water_fp.cg -profile arbfp1 -O3 -fastmath -fastprecision -o water_fp_arbfp1.txt
cgc -entry water_fp water_fp.cg -profile arbfp1 -O3 -fastmath -fastprecision -DUSE_FOG -o water_fp_fog_arbfp1.txt
cgc -entry water_fp water_fp.cg -profile arbfp1 -O3 -fastmath -fastprecision -DUSE_DIFFUSE -o water_fp_diffuse_arbfp1.txt
cgc -entry water_fp water_fp.cg -profile arbfp1 -O3 -fastmath -fastprecision -DUSE_DIFFUSE -DUSE_FOG -o water_fp_diffuse_fog_arbfp1.txt
cgc -entry water_fp water_fp.cg -profile ps_2_0 -O3 -fastmath -fastprecision -o water_fp_ps_2_0.txt
cgc -entry water_fp water_fp.cg -profile ps_2_0 -O3 -fastmath -fastprecision -DUSE_DIFFUSE -o water_fp_diffuse_ps_2_0.txt