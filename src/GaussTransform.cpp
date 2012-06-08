#include "main.h"
#include "GaussTransform.h"
#include "Permutohedral.h"
#include "DenseGrid.h"
#include "GKDTree.h"
#include "Arithmetic.h"
#include "Geometry.h"
#include "Color.h"
#include "Statistics.h"
#include "Convolve.h"
#include "header.h"

void GaussTransform::help() {
    pprintf("-gausstransform evaluates a weighted sum of Gaussians at a discrete"
            " set of positions. The positions are given by the top image on the"
            " stack, the locations of the Gaussians are given by the second image, and"
            " the weights of the Gaussians are given by the third. There are"
            " several ways to accelerate a Gauss transform, therefore"
            " -gausstransform takes one argument to indicate the method to"
            " use, and then one argument per channel in the second image in the"
            " stack to indicate the standard deviation of the desired Gaussian in"
            " that dimension. Methods available are: exact (slow!); grid (the"
            " bilateral grid of Paris et al.); permutohedral (the permutohedral"
            " lattice of Adams et al.); and gkdtree (the gaussian kdtree of Adams et"
            " al.). If only one argument is given, the standard deviations used are"
            " all one. If two arguments are given, the standard deviation is the"
            " same in each dimension.\n"
            "\n"
            "Usage: ImageStack -load pics/dog1.jpg -evalchannels [0] [1] [2] 1 \\\n"
            "                  -dup -evalchannels x y [0] [1] [2] -dup \\\n"
            "                  -gausstransform permutohedral 4 4 0.1 0.1 0.1 \\\n"
            "                  -evalchannels [0]/[3] [1]/[3] [2]/[3] \\\n"
            "                  -save bilateral_filtered_dog.jpg\n");
}


void GaussTransform::parse(vector<string> args) {
    Method m;

    assert(args.size() > 0, "-gausstransform takes at least one argument");
 
    if (args[0] == "exact") {
        m = EXACT;
    } else if (args[0] == "grid") {
        m = GRID;
    } else if (args[0] == "permutohedral") {
        m = PERMUTOHEDRAL;
    } else if (args[0] == "gkdtree") {
        m = GKDTREE;
    } else {
        panic("Unknown method %s\n", args[0].c_str());
    }

    vector<float> sigmas;
    if (args.size() == 1) {
        sigmas = vector<float>(stack(0).channels, 1.0);
    } else if (args.size() == 2) {
        sigmas = vector<float>(stack(0).channels, readFloat(args[1]));
    } else if ((int)args.size() == 1 + stack(1).channels) {
        for (int i = 0; i < stack(0).channels; i++) {
            sigmas.push_back(readFloat(args[i+1]));
        }
    } else {
        panic("-gausstransform takes one argument, two arguments, or one plus the"
              " number of channels in the second image on the stack arguments\n");
    }
 
    Image im = apply(stack(0), stack(1), stack(2), sigmas, m);
    pop();
    push(im);
}

Image GaussTransform::apply(Image slice, Image splat, Image values,
                            vector<float> sigmas,
                            GaussTransform::Method method) {
    assert(splat.width == values.width &&
           splat.height == values.height &&
           splat.frames == values.frames,
           "Weights and locations of the Gaussians must be the same size\n");
    assert(slice.channels == splat.channels,
           "The evaluation locations and the locations of the Gaussians must have"
           " the same number of channels.\n");

    vector<float> invVar(sigmas.size());
    vector<float> invSigma(sigmas.size());

    for (size_t i = 0; i < sigmas.size(); i++) {
        invVar[i] = 0.5f/(sigmas[i]*sigmas[i]);
        invSigma[i] = 1.0f/sigmas[i];
    }

    switch (method) {
    case EXACT: {
        Image out(slice.width, slice.height, 
		     slice.frames, values.channels);
        for (int t1 = 0; t1 < slice.frames; t1++) {
            for (int t2 = 0; t2 < splat.frames; t2++) {
                for (int y1 = 0; y1 < slice.height; y1++) {
                    for (int y2 = 0; y2 < splat.height; y2++) {
                        for (int x1 = 0; x1 < slice.width; x1++) {
                            for (int x2 = 0; x2 < splat.width; x2++) {
                                float dist = 0; 
                                for (int c = 0; c < splat.channels; c++) {
				    float d = (slice(x1, y1, t1, c) -
					       splat(x2, y2, t2, c));
				    dist += d*d * invVar[c];
                                }
                                float weight = fastexp(-dist);
				
                                for (int c = 0; c < values.channels; c++) {
				    out(x1, y1, t1, c) += weight * values(x2, y2, t2, c);
                                }
                            }
                        }
                    }
                }
            }
        }
        return out;
    }
    case PERMUTOHEDRAL: {
        // Create lattice
        PermutohedralLattice lattice(splat.channels, values.channels,
                                     values.width*values.height*values.frames);

        // Splat into the lattice
        //printf("Splatting...\n");

        vector<float> pos(splat.channels);
	vector<float> val(values.channels);
        for (int t = 0; t < splat.frames; t++) {
            for (int y = 0; y < splat.height; y++) {
                for (int x = 0; x < splat.width; x++) {
                    for (int c = 0; c < splat.channels; c++) {
                        pos[c] = splat(x, y, t, c) * invSigma[c];
                    }
		    for (int c = 0; c < values.channels; c++) {
			val[c] = values(x, y, t, c);
		    }
                    lattice.splat(&pos[0], &val[0]);
                }
            }
        }

        // Blur the lattice
        //printf("Blurring...\n");
        lattice.blur();

        // Slice from the lattice
        //printf("Slicing...\n");

        Image out(slice.width, slice.height, slice.frames, values.channels);

        if (slice == splat) {
            lattice.beginSlice();
            for (int t = 0; t < slice.frames; t++) {
                for (int y = 0; y < slice.height; y++) {		    
                    for (int x = 0; x < slice.width; x++) {
                        lattice.slice(&val[0]);
			for (int c = 0; c < out.channels; c++) {
			    out(x, y, t, c) = val[c];
			}
                    }
                }
            }
        } else {
            for (int t = 0; t < slice.frames; t++) {
                for (int y = 0; y < slice.height; y++) {
                    for (int x = 0; x < slice.width; x++) {
                        for (int c = 0; c < slice.channels; c++) {
                            pos[c] = slice(x, y, t, c) * invSigma[c];
                        }
                        lattice.slice(&pos[0], &val[0]);
			for (int c = 0; c < out.channels; c++) {
			    out(x, y, t, c) = val[c];
			}			    
                    }
                }
            }
        }


        return out;
    }
    case GRID: {
        // Create grid
        DenseGrid grid(splat.channels, values.channels, 5);

        // Splat into the grid

        vector<float> pos(splat.channels);
	vector<float> val(values.channels);

        //printf("Allocating...\n");
        for (int t = 0; t < splat.frames; t++) {
            for (int y = 0; y < splat.height; y++) {
                for (int x = 0; x < splat.width; x++) {
                    for (int c = 0; c < splat.channels; c++) {
                        pos[c] = splat(x, y, t, c) * invSigma[c];
                    }
                    grid.preview(&pos[0]);
                }
            }
        }
        if (splat != slice) {
            for (int t = 0; t < slice.frames; t++) {
                for (int y = 0; y < slice.height; y++) {
                    for (int x = 0; x < slice.width; x++) {
                        for (int c = 0; c < slice.channels; c++) {
                            pos[c] = slice(x, y, t, c) * invSigma[c];
                        }
                        grid.preview(&pos[0]);
                    }
                }
            }
        }

        //printf("Splatting...\n");
        for (int t = 0; t < splat.frames; t++) {
            for (int y = 0; y < splat.height; y++) {
                for (int x = 0; x < splat.width; x++) {
                    for (int c = 0; c < splat.channels; c++) {
                        pos[c] = splat(x, y, t, c) * invSigma[c];
                    }
		    for (int c = 0; c < values.channels; c++) {
			val[c] = values(x, y, t, c);
		    }
                    grid.splat(&pos[0], &val[0]);
                }
            }
        }

        // Blur the grid
        grid.blur();

        // Slice from the grid
        //printf("Slicing...\n");

        Image out(slice.width, slice.height, slice.frames, values.channels);

        for (int t = 0; t < slice.frames; t++) {
            for (int y = 0; y < slice.height; y++) {
                for (int x = 0; x < slice.width; x++) {
                    for (int c = 0; c < slice.channels; c++) {
                        pos[c] = slice(x, y, t, c) * invSigma[c];
                    }
                    grid.slice(&pos[0], &val[0]);
		    for (int c = 0; c < out.channels; c++) {
			out(x, y, t, c) = val[c];
		    }
                }
            }
        }

        return out;
    }
    case GKDTREE: {
        printf("Building...\n");

	// The gkdtree requires channels to be densely packed
	vector<float> ref(splat.channels*splat.width*splat.height*splat.frames);
        vector<float *> points(splat.width*splat.height*splat.frames);
	{
	    int i = 0;
	    for (int t = 0; t < splat.frames; t++) {            
		for (int y = 0; y < splat.height; y++) {
		    for (int x = 0; x < splat.width; x++) {	      
			float *ptr = &ref[i*splat.channels];
			for (int c = 0; c < splat.channels; c++) {
			    ptr[c] = invSigma[c] * splat(x, y, t, c);
			}
			points[i] = ptr;
			i++;
		    }
		}
	    }  
	}

        GKDTree tree(splat.channels, &points[0], points.size(), 2*0.707107);

        tree.finalize();

        printf("%d leaves.\n", tree.getLeaves());
        printf("Splatting...");

        int SPLAT_ACCURACY = 4; 
        int SLICE_ACCURACY = 64;
 
        vector<int> indices(max(SPLAT_ACCURACY, SLICE_ACCURACY));
        vector<float> weights(max(SPLAT_ACCURACY, SLICE_ACCURACY));
        vector<double> leafValues(tree.getLeaves()*values.channels);

        // Compute expected number of samples to arrive at each leaf
        // and divide by it to keep values at leaves within sane
        // bounds.
        float leafScale = tree.getLeaves();
        leafScale /= SPLAT_ACCURACY;
        leafScale /= splat.frames;
        leafScale /= splat.channels;
        leafScale /= splat.height;
        printf("Multiplying all weights by %f\n", leafScale);

	float *refPtr = &ref[0];

        for (int t = 0; t < values.frames; t++) {
            printf("."); 
            fflush(stdout);
            for (int y = 0; y < values.height; y++) {
                for (int x = 0; x < values.width; x++) {
                    int results = tree.gaussianLookup(refPtr, 
						      &indices[0], 
						      &weights[0], 
						      SPLAT_ACCURACY);
		    refPtr += splat.channels;
                    for (int i = 0; i < results; i++) {
                        double w = weights[i];

                        // For numerical stability, disallow huge weights
                        if (w > 1e6) w = 1e6;
                        // Don't corrupt the tree with nans
                        if (!isfinite(w)) continue;

                        w *= leafScale;

                        double *vPtr = &leafValues[indices[i]*values.channels];
                        for (int c = 0; c < values.channels; c++) {
                            vPtr[c] += values(x, y, t, c)*w;
                        }
                    }
                }
            }
        }
        printf("\n");

        Image out(slice.width, slice.height, slice.frames, values.channels);

        vector<float> pos(slice.channels);
        vector<double> outDbl(out.channels);

        printf("Slicing...");
        for (int t = 0; t < out.frames; t++) {
            printf("."); 
            fflush(stdout);
            for (int y = 0; y < out.height; y++) {
                for (int x = 0; x < out.width; x++) {
                    for (int c = 0; c < slice.channels; c++) {
                        pos[c] = slice(x, y, t, c) * invSigma[c];
                    }
                    int results = tree.gaussianLookup(&pos[0], 
						      &indices[0], 
						      &weights[0],
						      SLICE_ACCURACY);
                    for (int c = 0; c < out.channels; c++) {
                        outDbl[c] = 0;
                    }
                    for (int i = 0; i < results; i++) {
                        double w = weights[i];
                        // For numerical stability, disallow huge weights
                        if (w > 1e6) w = 1e6;
                        // Don't corrupt the output with nans
                        if (!isfinite(w)) continue;

                        double *vPtr = &leafValues[indices[i]*values.channels];
                        for (int c = 0; c < out.channels; c++) {
                            outDbl[c] += vPtr[c]*w;
                        }
                    }                    

                    for (int c = 0; c < out.channels; c++) {
                        out(x, y, t, c) = (float)outDbl[c];
                    }
                }
            }
        }
        printf("\n");

        return out;
    }
    default: {
        panic("This Gauss transform method not yet implemented\n");
        return Image();
    }
    }
}


void JointBilateral::help() {
    pprintf("-jointbilateral blurs the top image in the second without crossing"
            " boundaries in the second image in the stack. It takes up to five"
            " arguments: the color standard deviation of the filter, the standard"
            " deviations in width, height, and frames, and the method to use (see"
            " -gausstransform for a description of the methods). If the method is"
            " omitted it automatically chooses an appropriate one. Temporal standard"
            " deviation defaults to zero, and standard deviation in height defaults"
            " to the same as the standard deviation in width\n"
            "\n"
            "Usage: ImageStack -load ref.jpg -load im.jpg -jointbilateral 0.1 4\n");
}

void JointBilateral::parse(vector<string> args) {
    float colorSigma, filterWidth, filterHeight, filterFrames;
    GaussTransform::Method m = GaussTransform::AUTO;

    if (args.size() < 2 || args.size() > 5) {
        panic("-jointbilateral takes from two to five arguments\n");
        return;
    }

    colorSigma = readFloat(args[0]);
    filterWidth = filterHeight = readFloat(args[1]);

    if (args.size() > 2) { filterHeight = readFloat(args[2]); }
    if (args.size() > 3) { filterFrames = readFloat(args[3]); }
    if (args.size() > 4) {
        if (args[4] == "exact") {
            m = GaussTransform::EXACT;
        } else if (args[4] == "grid") {
            m = GaussTransform::GRID;
        } else if (args[4] == "permutohedral") {
            m = GaussTransform::PERMUTOHEDRAL;
        } else if (args[4] == "gkdtree") {
            m = GaussTransform::GKDTREE;
        } else {
            panic("Unknown method %s\n", args[4].c_str());
        }
    }

    apply(stack(0), stack(1), filterWidth, filterHeight, filterFrames, colorSigma, m);
}

void JointBilateral::apply(Image im, Image ref,
                           float filterWidth, float filterHeight, float filterFrames, float colorSigma,
                           GaussTransform::Method method) {
    assert(im.width == ref.width &&
           im.height == ref.height &&
           im.frames == ref.frames,
           "Image and reference must be the same size\n");

    if (im.width == 1) { filterWidth = 0; }
    if (im.height == 1) { filterHeight = 0; }
    if (im.frames == 1) { filterFrames = 0; }

    if (im.width > 1 && filterWidth == 0) {
        // filter each column independently
        for (int x = 0; x < im.width; x++) {
            apply(im.column(x), ref.column(x), 0, filterHeight, filterFrames, colorSigma, method);
        }
        return;
    }

    if (im.height > 1 && filterHeight == 0) {
        // filter each row independently
        for (int y = 0; y < im.height; y++) {
            apply(im.row(y), ref.row(y), filterWidth, 0, filterFrames, colorSigma, method);
        }
        return;
    }

    if (im.frames > 1 && filterFrames == 0) {
        // filter each frame independently
        for (int t = 0; t < im.frames; t++) {
            apply(im.frame(t), ref.frame(t), filterWidth, filterHeight, 0, colorSigma, method);
        }
        return;
    }

    int posChannels = ref.channels;
    bool filterX = true, filterY = true, filterT = true;
    if (im.width == 1 || isinf(filterWidth) || filterWidth > 10*im.width) { filterX = false; }
    if (im.height == 1 || isinf(filterHeight) || filterHeight > 10*im.height) { filterY = false; }
    if (im.frames == 1 || isinf(filterFrames) || filterFrames > 10*im.frames) { filterT = false; }
    if (filterX) { posChannels++; }
    if (filterY) { posChannels++; }
    if (filterT) { posChannels++; }

    // make the spatial filter
    int filterSizeX = filterX ? ((int)(filterWidth * 6 + 1) | 1) : 1;
    int filterSizeY = filterY ? ((int)(filterHeight * 6 + 1) | 1) : 1;
    int filterSizeT = filterT ? ((int)(filterFrames * 6 + 1) | 1) : 1;

    if (method == GaussTransform::AUTO) {
        // This is approximately the selection procedure given by
        // figure 7 in "Fast High-Dimensional Gaussian Filtering using
        // the Permutohedral Lattice"
        if (filterSizeT * filterSizeX * filterSizeY < 16) {
            method = GaussTransform::EXACT;
        } else {
            if (posChannels <= 4) { method = GaussTransform::GRID; }
            else if (posChannels <= 10) { method = GaussTransform::PERMUTOHEDRAL; }
            else { method = GaussTransform::GKDTREE; }
        }
    }

    if (method == GaussTransform::EXACT) {

        Image out(im.width, im.height, im.frames, im.channels);

        Image filter(filterSizeX, filterSizeY, filterSizeT, 1);

        for (int t = 0; t < filter.frames; t++) {
	    for (int y = 0; y < filter.height; y++) {
		for (int x = 0; x < filter.width; x++) {
                    float dt = (t - filter.frames / 2) / filterFrames;
                    float dx = (x - filter.width / 2) / filterWidth;
                    float dy = (y - filter.height / 2) / filterHeight;
                    if (!filterT) { dt = 0; }
                    if (!filterX) { dx = 0; }
                    if (!filterY) { dy = 0; }
                    float distance = dt * dt + dx * dx + dy * dy;
                    float value = expf(-distance/2);
                    filter(x, y, t, 0) = value;
                }
            }
        }

        // convolve
        int xoff = (filter.width - 1)/2;
        int yoff = (filter.height - 1)/2;
        int toff = (filter.frames - 1)/2;

        float colorSigmaMult = 0.5f/(colorSigma*colorSigma);

        for (int t = 0; t < im.frames; t++) {
            for (int y = 0; y < im.height; y++) {
                for (int x = 0; x < im.width; x++) {
                    float totalWeight = 0;
                    for (int dt = -toff; dt <= toff; dt++) {
                        int imt = t + dt;
                        if (imt < 0) { continue; }
                        if (imt >= im.frames) { break; }
                        int filtert = dt + toff;
                        for (int dy = -yoff; dy <= yoff; dy++) {
                            int imy = y + dy;
                            if (imy < 0) { continue; }
                            if (imy >= im.height) { break; }
                            int filtery = dy + yoff;
                            for (int dx = -xoff; dx <= xoff; dx++) {
                                int imx = x + dx;
                                if (imx < 0) { continue; }
                                if (imx >= im.width) { break; }
                                int filterx = dx + xoff;
                                float weight = filter(filterx, filtery, filtert, 0);
                                float colorDistance = 0;
                                for (int c = 0; c < ref.channels; c++) {
                                    float diff = (ref(imx, imy, imt, c) - ref(x, y, t, c));
                                    colorDistance += diff * diff * colorSigmaMult;
                                }
                                weight *= fastexp(-colorDistance);
                                totalWeight += weight;
                                for (int c = 0; c < im.channels; c++) {
                                    out(x, y, t, c) += weight * im(imx, imy, imt, c);
                                }
                            }
                        }
                    }
                    for (int c = 0; c < im.channels; c++) {
                        out(x, y, t, c) /= totalWeight;
                    }
                }
            }
        }

        Paste::apply(im, out, 0, 0);

        return;
    }

    // Convert the problem to a gauss transform.  We could
    // theoretically be faster by calling the various Gauss transform
    // methods directly, but it would involve copy pasting large
    // amounts of code with minor tweaks.

    Image splat(im.width, im.height, im.frames, posChannels);
    Image values(im.width, im.height, im.frames, im.channels+1);

    float invColorSigma = 1.0f/colorSigma;
    float invSigmaX = 1.0f/filterWidth;
    float invSigmaY = 1.0f/filterHeight;
    float invSigmaT = 1.0f/filterFrames;

    // Add a homogeneous channel
    for (int c = 0; c < im.channels; c++) {
	for (int t = 0; t < im.frames; t++) {
	    for (int y = 0; y < im.height; y++) {
		for (int x = 0; x < im.width; x++) {
		    values(x, y, t, c) = im(x, y, t, c);
                }
	    }
	}
    }
    values.channel(im.channels).set(1.0f);

    for (int c = 0; c < ref.channels; c++) {
	for (int t = 0; t < im.frames; t++) {
	    for (int y = 0; y < im.height; y++) {
		for (int x = 0; x < im.width; x++) {
		    splat(x, y, t, c) = im(x, y, t, c) * invColorSigma;
		}
	    }
	}
    }
    {
	int c = ref.channels;
	if (filterX) {
	    for (int t = 0; t < im.frames; t++) {
		for (int y = 0; y < im.height; y++) {
		    for (int x = 0; x < im.width; x++) {
			splat(x, y, t, c) = x * invSigmaX;
		    }
		}
	    }
	    c++;
	}
	if (filterY) {
	    for (int t = 0; t < im.frames; t++) {
		for (int y = 0; y < im.height; y++) {
		    for (int x = 0; x < im.width; x++) {
			splat(x, y, t, c) = y * invSigmaY;
		    }
		}
	    }
	    c++;
	}
	if (filterT) {
	    for (int t = 0; t < im.frames; t++) {
		for (int y = 0; y < im.height; y++) {
		    for (int x = 0; x < im.width; x++) {
			splat(x, y, t, c) = t * invSigmaT;
		    }
		}
	    }
	}
    }

    vector<float> sigmas(splat.channels, 1);
    values = GaussTransform::apply(splat, splat, values, sigmas, method);

    for (int t = 0; t < im.frames; t++) {
        for (int y = 0; y < im.height; y++) {
            for (int x = 0; x < im.width; x++) {
                float w = 1.0f/values(x, y, t, im.channels);
                for (int c = 0; c < im.channels; c++) {
		    im(x, y, t, c) = values(x, y, t, c) * w;
                }
            }
        }
    }
}


void Bilateral::help() {
    pprintf("-bilateral blurs the top image in the second without crossing"
            " boundaries. It takes up to five arguments: the color standard"
            " deviation of the filter, the standard deviations in width, height,"
            " and frames, and the method to use (see -gausstransform for a"
            " description of the methods). If the method is omitted it"
            " automatically chooses an appropriate one. Temporal standard"
            " deviation defaults to zero, and standard deviation in height defaults"
            " to the same as the standard deviation in width\n"
            "\n"
            "Usage: ImageStack -load im.jpg -bilateral 0.1 4\n");
}

void Bilateral::parse(vector<string> args) {
    float colorSigma, filterWidth, filterHeight, filterFrames;
    GaussTransform::Method m = GaussTransform::AUTO;

    if (args.size() < 2 || args.size() > 5) {
        panic("-bilateral takes from two to five arguments\n");
        return;
    }

    colorSigma = readFloat(args[0]);
    filterWidth = filterHeight = readFloat(args[1]);

    if (args.size() > 2) { filterHeight = readFloat(args[2]); }
    if (args.size() > 3) { filterFrames = readFloat(args[3]); }
    if (args.size() > 4) {
        if (args[4] == "exact") {
            m = GaussTransform::EXACT;
        } else if (args[4] == "grid") {
            m = GaussTransform::GRID;
        } else if (args[4] == "permutohedral") {
            m = GaussTransform::PERMUTOHEDRAL;
        } else if (args[4] == "gkdtree") {
            m = GaussTransform::GKDTREE;
        } else {
            panic("Unknown method %s\n", args[4].c_str());
        }
    }

    apply(stack(0), filterWidth, filterHeight, filterFrames, colorSigma, m);
}

void Bilateral::apply(Image image, float filterWidth, float filterHeight,
                      float filterFrames, float colorSigma,
                      GaussTransform::Method m) {
    JointBilateral::apply(image, image, filterWidth, filterHeight, filterFrames, colorSigma, m);
}




void BilateralSharpen::help() {
    printf("\n-bilateralsharpen sharpens using a bilateral filter to avoid ringing. The\n"
           "three arguments are the spatial and color standard deviations, and the sharpness.\n"
           "A sharpness of zero has no effect; a sharpness of 1 is significant.\n\n"
           "Usage: ImageStack -load input.jpg -bilateralsharpen 1.0 0.2 1 -save sharp.jpg\n\n");
}

void BilateralSharpen::parse(vector<string> args) {
    assert(args.size() == 3, "-bilateralsharpen takes three arguments");
    Image im = apply(stack(0), readFloat(args[0]), readFloat(args[1]), readFloat(args[2]));
    pop();
    push(im);
}

Image BilateralSharpen::apply(Image im, float spatialSigma, float colorSigma, float sharpness) {
    Image out = im.copy();
    Bilateral::apply(out, spatialSigma, spatialSigma, 0, colorSigma);

    float imWeight = 1+sharpness, outWeight = -sharpness;

    for (int c = 0; c < im.channels; c++) {
	for (int t = 0; t < im.frames; t++) {
	    for (int y = 0; y < im.height; y++) {
		for (int x = 0; x < im.width; x++) {
		    out(x, y, t, c) = im(x, y, t, c) * imWeight + out(x, y, t, c) * outWeight;
		}
	    }
	}
    }
    return out;
}



void ChromaBlur::help() {
    printf("\n-chromablur blurs an image in the chrominance channels only. It is a good way\n"
           "of getting rid of chroma noise without apparently blurring the image. The two\n"
           "arguments are the standard deviation in space and color of the bilateral filter.\n"
           "Usage: ImageStack -load input.jpg -chromablur 2 -save output.png\n\n");
}

void ChromaBlur::parse(vector<string> args) {
    assert(args.size() == 2, "-chromablur takes two arguments");
    Image im = apply(stack(0), readFloat(args[0]), readFloat(args[1]));
    pop();
    push(im);
}

Image ChromaBlur::apply(Image im, float spatialSigma, float colorSigma) {
    assert(im.channels == 3, "input must be a rgb image\n");
    Image yuv = ColorConvert::rgb2yuv(im);
    Image luminance = ColorConvert::rgb2y(im);

    // blur chrominance
    JointBilateral::apply(yuv, luminance, spatialSigma, spatialSigma, 0, colorSigma);

    // restore luminance
    yuv.channel(0).set(luminance);

    return ColorConvert::yuv2rgb(yuv);
}



void NLMeans::help() {
    pprintf("-nlmeans denoises an image using non-local means, by performing a PCA"
            " reduction on Gaussian weighted patches and then doing a"
            " joint-bilateral filter of the image with respect to those PCA-reduced"
            " patches. The four arguments required are the standard deviation of"
            " the Gaussian patches used, the number of dimensions to reduce the"
            " patches to, the spatial standard deviation of the filter, and the"
            " patch-space standard deviation of the filter. Tolga Tasdizen"
            " demonstrates in \"Principal Components for Non-Local Means Image"
            " Denoising\" that 6 dimensions work best most of the time. You can"
            " optionally add a fifth argument that specifies which method to use"
            " for the joint bilateral filter (see -gausstransform).\n"
            "\n"
            "Usage: ImageStack -load noisy.jpg -nlmeans 1.0 6 50 0.02\n");
}

void NLMeans::parse(vector<string> args) {
    assert(args.size() == 4 || args.size() == 5, "-nlmeans takes four or five arguments\n");

    float patchSize = readFloat(args[0]);
    int dimensions = readInt(args[1]);
    float spatialSigma = readFloat(args[2]);
    float patchSigma = readFloat(args[3]);

    GaussTransform::Method m = GaussTransform::AUTO;
    if (args.size() > 4) {
        if (args[4] == "exact") {
            m = GaussTransform::EXACT;
        } else if (args[4] == "grid") {
            m = GaussTransform::GRID;
        } else if (args[4] == "permutohedral") {
            m = GaussTransform::PERMUTOHEDRAL;
        } else if (args[4] == "gkdtree") {
            m = GaussTransform::GKDTREE;
        } else {
            panic("Unknown method %s\n", args[4].c_str());
        }
    }

    apply(stack(0), patchSize, dimensions, spatialSigma, patchSigma, m);

}

void NLMeans::apply(Image image, float patchSize, int dimensions,
                    float spatialSigma, float patchSigma,
                    GaussTransform::Method method) {

    Image filters = PatchPCA::apply(image, patchSize, dimensions);
    Image pca = Convolve::apply(image, filters, Convolve::Zero, Multiply::Inner);
    JointBilateral::apply(image, pca, spatialSigma, spatialSigma, INF, patchSigma);
};


void NLMeans3D::help() {
    pprintf("-nlmeans3d denoises a volume using non-local means, by performing a PCA"
            " reduction on Gaussian weighted patches and then doing a"
            " joint-bilateral filter of the image with respect to those PCA-reduced"
            " patches. The four arguments required are the standard deviation of"
            " the Gaussian patches used, the number of dimensions to reduce the"
            " patches to, the spatial standard deviation of the filter, and the"
            " patch-space standard deviation of the filter. Tolga Tasdizen"
            " demonstrates in \"Principal Components for Non-Local Means Image"
            " Denoising\" that 6 dimensions work best most of the time. You can"
            " optionally add a fifth argument that specifies which method to use"
            " for the joint bilateral filter (see -gausstransform).\n"
            "\n"
            "Usage: ImageStack -load volume.tmp -nlmeans3d 1.0 6 50 0.02\n");
}

void NLMeans3D::parse(vector<string> args) {
    assert(args.size() == 4 || args.size() == 5, "-nlmeans takes four or five arguments\n");

    float patchSize = readFloat(args[0]);
    int dimensions = readInt(args[1]);
    float spatialSigma = readFloat(args[2]);
    float patchSigma = readFloat(args[3]);

    GaussTransform::Method m = GaussTransform::AUTO;
    if (args.size() > 4) {
        if (args[4] == "exact") {
            m = GaussTransform::EXACT;
        } else if (args[4] == "grid") {
            m = GaussTransform::GRID;
        } else if (args[4] == "permutohedral") {
            m = GaussTransform::PERMUTOHEDRAL;
        } else if (args[4] == "gkdtree") {
            m = GaussTransform::GKDTREE;
        } else {
            panic("Unknown method %s\n", args[4].c_str());
        }
    }

    apply(stack(0), patchSize, dimensions, spatialSigma, patchSigma, m);

}

void NLMeans3D::apply(Image image, float patchSize, int dimensions,
                      float spatialSigma, float patchSigma,
                      GaussTransform::Method method) {

    Image filters = PatchPCA3D::apply(image, patchSize, dimensions);
    Image pca = Convolve::apply(image, filters, Convolve::Zero, Multiply::Inner);
    JointBilateral::apply(image, pca, spatialSigma, spatialSigma, INF, patchSigma);
};


#include "footer.h"
