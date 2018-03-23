/*
 *                Ineractive Access to folding Routines
 *
 *                c Ivo L Hofacker
 *                Vienna RNA package
 */

/** \file
 *  \brief RNAfold program source code
 *
 *  This code provides an interface for MFE and Partition function folding
 *  of single linear or circular RNA molecules.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include "ViennaRNA/fold.h"
#include "ViennaRNA/part_func.h"
#include "ViennaRNA/fold_vars.h"
#include "ViennaRNA/PS_dot.h"
#include "ViennaRNA/utils.h"
#include "ViennaRNA/file_utils.h"
#include "ViennaRNA/read_epars.h"
#include "ViennaRNA/centroid.h"
#include "ViennaRNA/MEA.h"
#include "ViennaRNA/params.h"
#include "ViennaRNA/constraints.h"
#include "ViennaRNA/constraints_SHAPE.h"
#include "ViennaRNA/constraints_ligand.h"
#include "ViennaRNA/structured_domains.h"
#include "ViennaRNA/unstructured_domains.h"
#include "ViennaRNA/file_formats.h"
#include "ViennaRNA/commands.h"
#include "ViennaRNA/equilibrium_probs.h"
#include "RNAfold_cmdl.h"
#include "gengetopt_helper.h"
#include "input_id_helpers.h"

#include "ViennaRNA/color_output.inc"

struct options {
  int         filename_full;
  char        *filename_delim;
  int         pf;
  int         noPS;
  int         noconv;
  int         lucky;
  int         MEA;
  double      MEAgamma;
  double      bppmThreshold;
  int         verbose;
  char        *ligandMotif;
  vrna_cmd_t  *cmds;
  vrna_md_t   md;
  dataset_id  id_control;

  char        *constraint_file;
  int         constraint_batch;
  int         constraint_enforce;
  int         constraint_canonical;

  int         shape;
  char        *shape_file;
  char        *shape_method;
  char        *shape_conversion;

  int         tofile;
  char        *output_file;
};


static char *annotate_ligand_motif(vrna_fold_compound_t *vc,
                                   const char           *structure);


static void
print_ligand_motifs(vrna_fold_compound_t  *vc,
                    const char            *structure,
                    const char            *structure_name,
                    FILE                  *output);


static void add_ligand_motif(vrna_fold_compound_t *vc,
                             char                 *motifstring,
                             int                  verbose,
                             unsigned int         options);


static char *
annotate_ud_motif(vrna_fold_compound_t  *vc,
                  const char            *structure);


static void
print_ud_motifs(vrna_fold_compound_t  *vc,
                const char            *structure,
                const char            *structure_name,
                FILE                  *output);


static void
add_ligand_motifs_dot(vrna_fold_compound_t  *fc,
                      vrna_ep_t             **prob_list,
                      vrna_ep_t             **mfe_list,
                      const char            *structure);


static void
add_ligand_motifs_to_list(vrna_ep_t       **list,
                          vrna_sc_motif_t *motifs);


static void
compute_MEA(vrna_fold_compound_t  *fc,
            double                MEAgamma,
            const char            *ligandMotif,
            int                   verbose,
            FILE                  *output);


static void
compute_centroid(vrna_fold_compound_t *fc,
                 const char           *ligandMotif,
                 int                  verbose,
                 FILE                 *output);


static void
apply_constraints(vrna_fold_compound_t  *fc,
                  const char            *constraints_file,
                  const char            **rec_rest,
                  int                   maybe_multiline,
                  int                   enforceConstraints,
                  int                   canonicalBPonly);


static char *
generate_filename(const char  *pattern,
                  const char  *def_name,
                  const char  *id,
                  const char  *filename_delim);


void
process_input(FILE            *input_stream,
              const char      *input_filename,
              struct options  *opt);


/*--------------------------------------------------------------------------*/

static void
postscript_layout(vrna_fold_compound_t  *fc,
                  const char            *orig_sequence,
                  const char            *structure,
                  const char            *SEQ_ID,
                  const char            *ligandMotif,
                  const char            *filename_delim,
                  int                   verbose)
{
  char      *filename_plot  = NULL;
  char      *annotation     = NULL;
  vrna_md_t *md             = &(fc->params->model_details);

  filename_plot = generate_filename("%s%sss.ps",
                                    "rna.ps",
                                    SEQ_ID,
                                    filename_delim);

  if (ligandMotif) {
    char *annote = annotate_ligand_motif(fc, structure);
    vrna_strcat_printf(&annotation, "%s", annote);
    free(annote);
  }

  if (fc->domains_up) {
    char *a = annotate_ud_motif(fc, structure);
    vrna_strcat_printf(&annotation, "%s", a);
    free(a);
  }

  (void)vrna_file_PS_rnaplot_a(orig_sequence, structure, filename_plot, annotation, NULL, md);

  free(annotation);
  free(filename_plot);
}


static void
ImFeelingLucky(vrna_fold_compound_t *fc,
               const char           *orig_sequence,
               const char           *SEQ_ID,
               int                  noPS,
               const char           *filename_delim,
               FILE                 *output,
               int                  istty_in)
{
  vrna_md_t *md = &(fc->params->model_details);

  vrna_init_rand();

  char      *filename_plot  = NULL;
  char      *s              = vrna_pbacktrack(fc);
  float     e               = vrna_eval_structure(fc, (const char *)s);
  if (output) {
    char *energy_string = NULL;
    if (istty_in)
      energy_string = vrna_strdup_printf("\n free energy = %6.2f kcal/mol", e);
    else
      energy_string = vrna_strdup_printf(" (%6.2f)", e);

    print_structure(output, s, energy_string);
    free(energy_string);
    (void)fflush(output);
  }

  if (!noPS) {
    filename_plot = generate_filename("%s%sss.ps",
                                      "rna.ps",
                                      SEQ_ID,
                                      filename_delim);

    (void)vrna_file_PS_rnaplot(orig_sequence, s, filename_plot, md);
    free(filename_plot);
  }

  free(s);
}


static char *
generate_filename(const char  *pattern,
                  const char  *def_name,
                  const char  *id,
                  const char  *filename_delim)
{
  char *filename, *ptr;

  if (id) {
    filename  = vrna_strdup_printf(pattern, id, filename_delim);
    ptr       = vrna_filename_sanitize(filename, filename_delim);
    free(filename);
    filename = ptr;
  } else {
    filename = strdup(def_name);
  }

  return filename;
}


static char **
collect_unnamed_options(struct RNAfold_args_info  *ggostruct,
                        int                       *num_files)
{
  char  **input_files = NULL;
  int   i;

  *num_files = 0;

  /* collect all unnamed options */
  if (ggostruct->inputs_num > 0) {
    input_files = (char **)vrna_realloc(input_files, sizeof(char *) * ggostruct->inputs_num);
    for (i = 0; i < ggostruct->inputs_num; i++)
      input_files[(*num_files)++] = strdup(ggostruct->inputs[i]);
  }

  return input_files;
}


static char **
append_input_files(struct RNAfold_args_info *ggostruct,
                   char                     **files,
                   int                      *numfiles)
{
  int i;

  if (ggostruct->infile_given) {
    files = (char **)vrna_realloc(files, sizeof(char *) * (*numfiles + ggostruct->infile_given));
    for (i = 0; i < ggostruct->infile_given; i++)
      files[(*numfiles)++] = strdup(ggostruct->infile_arg[i]);
  }

  return files;
}


void
init_default_options(struct options *opt)
{
  opt->filename_full  = 0;
  opt->filename_delim = NULL;
  opt->pf             = 0;
  opt->noPS           = 0;
  opt->noconv         = 0;
  opt->lucky          = 0;
  opt->MEA            = 0;
  opt->MEAgamma       = 1.;
  opt->bppmThreshold  = 1e-5;
  opt->verbose        = 0;
  opt->ligandMotif    = NULL;
  opt->cmds           = NULL;
  set_model_details(&(opt->md));

  opt->constraint_file      = NULL;
  opt->constraint_batch     = 0;
  opt->constraint_enforce   = 0;
  opt->constraint_canonical = 0;

  opt->shape            = 0;
  opt->shape_file       = NULL;
  opt->shape_method     = NULL;
  opt->shape_conversion = NULL;

  opt->tofile       = 0;
  opt->output_file  = NULL;
}


int
main(int  argc,
     char *argv[])
{
  struct  RNAfold_args_info args_info;
  char                      **input_files;
  int                       num_input;
  struct  options           opt;

  num_input = 0;

  init_default_options(&opt);

  /*
   #############################################
   # check the command line parameters
   #############################################
   */
  if (RNAfold_cmdline_parser(argc, argv, &args_info) != 0)
    exit(1);

  /* get basic set of model details */
  ggo_get_md_eval(args_info, opt.md);
  ggo_get_md_fold(args_info, opt.md);
  ggo_get_md_part(args_info, opt.md);
  ggo_get_circ(args_info, opt.md.circ);

  /* check dangle model */
  if ((opt.md.dangles < 0) || (opt.md.dangles > 3)) {
    vrna_message_warning("required dangle model not implemented, falling back to default dangles=2");
    opt.md.dangles = dangles = 2;
  }

  /* SHAPE reactivity data */
  ggo_get_SHAPE(args_info, opt.shape, opt.shape_file, opt.shape_method, opt.shape_conversion);

  ggo_get_id_control(args_info, opt.id_control, "Sequence", "sequence", "_", 4, 1);

  ggo_get_constraints_settings(args_info,
                               fold_constrained,
                               opt.constraint_file,
                               opt.constraint_enforce,
                               opt.constraint_batch);

  /* enforce canonical base pairs in any case? */
  if (args_info.canonicalBPonly_given)
    opt.constraint_canonical = 1;

  /* do not convert DNA nucleotide "T" to appropriate RNA "U" */
  if (args_info.noconv_given)
    opt.noconv = 1;

  /* always look on the bright side of life */
  if (args_info.ImFeelingLucky_given)
    opt.md.uniq_ML = opt.lucky = opt.pf = st_back = 1;

  /* set the bppm threshold for the dotplot */
  if (args_info.bppmThreshold_given)
    opt.bppmThreshold = MIN2(1., MAX2(0., args_info.bppmThreshold_arg));

  /* do not produce postscript output */
  if (args_info.noPS_given)
    opt.noPS = 1;

  /* partition function settings */
  if (args_info.partfunc_given) {
    opt.pf = 1;
    if (args_info.partfunc_arg != 1)
      opt.md.compute_bpp = do_backtrack = args_info.partfunc_arg;
    else
      opt.md.compute_bpp = do_backtrack = 1;
  }

  /* MEA (maximum expected accuracy) settings */
  if (args_info.MEA_given) {
    opt.pf = opt.MEA = 1;
    if (args_info.MEA_arg != -1)
      opt.MEAgamma = args_info.MEA_arg;
  }

  if (args_info.layout_type_given)
    rna_plot_type = args_info.layout_type_arg;

  if (args_info.verbose_given)
    opt.verbose = 1;

  if (args_info.outfile_given) {
    opt.tofile = 1;
    if (args_info.outfile_arg)
      opt.output_file = strdup(args_info.outfile_arg);
  }

  if (args_info.motif_given)
    opt.ligandMotif = strdup(args_info.motif_arg);

  if (args_info.commands_given)
    opt.cmds = vrna_file_commands_read(args_info.commands_arg, VRNA_CMD_PARSE_DEFAULTS);

  /* filename sanitize delimiter */
  if (args_info.filename_delim_given)
    opt.filename_delim = strdup(args_info.filename_delim_arg);
  else if (get_id_delim(opt.id_control))
    opt.filename_delim = strdup(get_id_delim(opt.id_control));

  if ((opt.filename_delim) && isspace(*opt.filename_delim)) {
    free(opt.filename_delim);
    opt.filename_delim = NULL;
  }

  /* full filename from FASTA header support */
  if (args_info.filename_full_given)
    opt.filename_full = 1;

  input_files = collect_unnamed_options(&args_info, &num_input);
  input_files = append_input_files(&args_info, input_files, &num_input);

  /* free allocated memory of command line data structure */
  RNAfold_cmdline_parser_free(&args_info);


  /*
   #############################################
   # begin initializing
   #############################################
   */
  if (opt.md.circ && opt.md.gquad) {
    vrna_message_error("G-Quadruplex support is currently not available for circular RNA structures");
    exit(EXIT_FAILURE);
  }

  if (opt.md.circ && opt.md.noLP)
    vrna_message_warning("depending on the origin of the circular sequence, some structures may be missed when using --noLP\n"
                         "Try rotating your sequence a few times");

  /* process input files or handle input from stdin */
  if (num_input > 0) {
    for (int i = 0; i < num_input; i++) {
      FILE *input_stream = fopen((const char *)input_files[i], "r");
      if (!input_stream)
        vrna_message_error("Unable to open %d. input file \"%s\" for reading", i + 1,
                           input_files[i]);

      if (opt.verbose)
        vrna_message_info(stderr, "Processing %d. input file \"%s\"", i + 1, input_files[i]);

      process_input(input_stream, (const char *)input_files[i], &opt);

      fclose(input_stream);
      free(input_files[i]);
    }
  } else {
    process_input(stdin, NULL, &opt);
  }

  free(input_files);
  free(opt.constraint_file);
  free(opt.ligandMotif);
  free(opt.shape_method);
  free(opt.shape_conversion);
  free(opt.filename_delim);
  vrna_commands_free(opt.cmds);

  free_id_data(opt.id_control);

  return EXIT_SUCCESS;
}


/* main loop that processes an input stream */
void
process_input(FILE            *input_stream,
              const char      *input_filename,
              struct options  *opt)
{
  int           istty_in  = isatty(fileno(input_stream));
  int           istty_out = isatty(fileno(stdout));

  unsigned int  read_opt = 0;

  /* print user help if we get input from tty */
  if (istty_in && istty_out) {
    if (fold_constrained) {
      vrna_message_constraint_options_all();
      vrna_message_input_seq("Input sequence (upper or lower case) followed by structure constraint");
    } else {
      vrna_message_input_seq_simple();
    }
  }

  /* set options we wanna pass to vrna_file_fasta_read_record() */
  if (istty_in)
    read_opt |= VRNA_INPUT_NOSKIP_BLANK_LINES;

  if (!fold_constrained)
    read_opt |= VRNA_INPUT_NO_REST;

  /* main loop that processes each record obtained from input stream */
  while (1) {
    FILE          *output = NULL;
    double        energy, min_en;
    char          *SEQ_ID = NULL;
    char          *v_file_name = NULL;
    int           maybe_multiline = 0;
    char          *rec_sequence, *rec_id, **rec_rest, *mfe_structure, *orig_sequence,
                  *tmp_string;
    unsigned int  rec_type, length;

    rec_id    = mfe_structure = orig_sequence = tmp_string = NULL;
    rec_rest  = NULL;

    rec_type = vrna_file_fasta_read_record(&rec_id,
                                           &rec_sequence,
                                           &rec_rest,
                                           input_stream,
                                           read_opt);

    if (rec_type & (VRNA_INPUT_ERROR | VRNA_INPUT_QUIT))
      break;

    /*
     ########################################################
     # init everything according to the data we've read
     ########################################################
     */
    if (rec_id) {
      maybe_multiline = 1;
      /* remove '>' from FASTA header */
      rec_id = memmove(rec_id, rec_id + 1, strlen(rec_id));
    }

    /* construct the sequence ID */
    set_next_id(&rec_id, opt->id_control);
    SEQ_ID = fileprefix_from_id(rec_id, opt->id_control, opt->filename_full);

    if (opt->tofile) {
      /* prepare the file name */
      if (opt->output_file)
        v_file_name = vrna_strdup_printf("%s", opt->output_file);
      else
        v_file_name = (SEQ_ID) ?
                      vrna_strdup_printf("%s.fold", SEQ_ID) :
                      vrna_strdup_printf("RNAfold_output.fold");

      tmp_string = vrna_filename_sanitize(v_file_name, opt->filename_delim);
      free(v_file_name);
      v_file_name = tmp_string;

      if (input_filename && !strcmp(input_filename, v_file_name))
        vrna_message_error("Input and output file names are identical");

      output = fopen((const char *)v_file_name, "a");
      if (!output)
        vrna_message_error("Failed to open file for writing");
    } else {
      output = stdout;
    }

    /* convert DNA alphabet to RNA if not explicitely switched off */
    if (!opt->noconv)
      vrna_seq_toRNA(rec_sequence);

    /* store case-unmodified sequence */
    orig_sequence = strdup(rec_sequence);
    /* convert sequence to uppercase letters only */
    vrna_seq_toupper(rec_sequence);

    vrna_fold_compound_t *vc = vrna_fold_compound(rec_sequence, &(opt->md), VRNA_OPTION_DEFAULT);

    length = vc->length;

    if (istty_in && istty_out)
      vrna_message_info(stdout, "length = %d\n", length);

    mfe_structure = (char *)vrna_alloc(sizeof(char) * (length + 1));

    /* parse the rest of the current dataset to obtain a structure constraint */
    if (fold_constrained) {
      apply_constraints(vc,
                        opt->constraint_file,
                        (const char **)rec_rest,
                        maybe_multiline,
                        opt->constraint_enforce,
                        opt->constraint_canonical);
    }

    if (opt->shape) {
      vrna_constraints_add_SHAPE(vc,
                                 opt->shape_file,
                                 opt->shape_method,
                                 opt->shape_conversion,
                                 opt->verbose,
                                 VRNA_OPTION_DEFAULT);
    }

    if (opt->ligandMotif) {
      add_ligand_motif(vc,
                       opt->ligandMotif,
                       opt->verbose,
                       VRNA_OPTION_MFE | ((opt->pf) ? VRNA_OPTION_PF : 0));
    }

    if (opt->cmds)
      vrna_commands_apply(vc,
                          opt->cmds,
                          VRNA_CMD_PARSE_DEFAULTS);

    /*
     ########################################################
     # begin actual computations
     ########################################################
     */
    min_en = (double)vrna_mfe(vc, mfe_structure);

    /* check whether the constraint allows for any solution */
    if ((fold_constrained && opt->constraint_file) || (opt->cmds)) {
      if (min_en == (double)(INF / 100.)) {
        vrna_message_error(
          "Supplied structure constraints create empty solution set for sequence:\n%s",
          orig_sequence);
        exit(EXIT_FAILURE);
      }
    }

    if (output) {
      print_fasta_header(output, rec_id);
      fprintf(output, "%s\n", orig_sequence);
    }

    if (!opt->lucky) {
      if (output) {
        char *msg = NULL;
        if (istty_in && istty_out)
          msg = vrna_strdup_printf("\n minimum free energy = %6.2f kcal/mol", min_en);
        else
          msg = vrna_strdup_printf(" (%6.2f)", min_en);

        print_structure(output, mfe_structure, msg);

        if ((opt->ligandMotif) && (opt->verbose))
          print_ligand_motifs(vc, mfe_structure, "MFE", output);

        if ((vc->domains_up) && (opt->verbose))
          print_ud_motifs(vc, mfe_structure, "MFE", output);

        free(msg);
        (void)fflush(output);
      }

      if (!opt->noPS) {
        postscript_layout(vc,
                          orig_sequence,
                          mfe_structure,
                          SEQ_ID,
                          opt->ligandMotif,
                          opt->filename_delim,
                          opt->verbose);
      }
    }

    if (length > 2000)
      vrna_mx_mfe_free(vc);

    if (opt->pf) {
      char *pf_struc = (char *)vrna_alloc(sizeof(char) * (length + 1));
      if (vc->params->model_details.dangles % 2) {
        int dang_bak = vc->params->model_details.dangles;
        vc->params->model_details.dangles = 2;   /* recompute with dangles as in pf_fold() */
        min_en                            = vrna_eval_structure(vc, mfe_structure);
        vc->params->model_details.dangles = dang_bak;
      }

      vrna_exp_params_rescale(vc, &min_en);

      if (length > 2000)
        vrna_message_info(stderr, "scaling factor %f", vc->exp_params->pf_scale);

      (void)fflush(output);

      energy = (double)vrna_pf(vc, pf_struc);

      /* in case we abort because of floating point errors */
      if (length > 1600)
        vrna_message_info(stderr, "free energy = %8.2f", energy);

      if (opt->lucky) {
        ImFeelingLucky(vc,
                       orig_sequence,
                       SEQ_ID,
                       opt->noPS,
                       opt->filename_delim,
                       output,
                       istty_in && istty_out);
      } else {
        if (opt->md.compute_bpp) {
          if (output) {
            char *msg = NULL;
            if (istty_in && istty_out)
              msg = vrna_strdup_printf("\n free energy of ensemble = %6.2f kcal/mol", energy);
            else
              msg = vrna_strdup_printf(" [%6.2f]", energy);

            print_structure(output, pf_struc, msg);
            free(msg);
          }

          char  *filename_dotplot = NULL;
          plist *pl1, *pl2;

          /* generate initial element probability lists for dot-plot */
          pl1 = vrna_plist_from_probs(vc, opt->bppmThreshold);
          pl2 = vrna_plist(mfe_structure, 0.95 * 0.95);

          /* add ligand motif annotation if necessary */
          if (opt->ligandMotif)
            add_ligand_motifs_dot(vc, &pl1, &pl2, mfe_structure);

          /* generate dot-plot file name */
          filename_dotplot = generate_filename("%s%sdp.ps",
                                               "dot.ps",
                                               SEQ_ID,
                                               opt->filename_delim);

          if (filename_dotplot) {
            vrna_plot_dp_EPS(filename_dotplot,
                             orig_sequence,
                             pl1,
                             pl2,
                             NULL,
                             VRNA_PLOT_PROBABILITIES_DEFAULT);
          }

          free(filename_dotplot);
          free(pl2);

          /* compute stack probabilities and generate dot-plot */
          if (opt->md.compute_bpp == 2) {
            char *filename_stackplot = generate_filename("%s%sdp2.ps",
                                                         "dot2.ps",
                                                         SEQ_ID,
                                                         opt->filename_delim);

            pl2 = vrna_stack_prob(vc, 1e-5);

            if (filename_stackplot)
              PS_dot_plot_list(orig_sequence, filename_stackplot, pl1, pl2,
                               "Probabilities for stacked pairs (i,j)(i+1,j-1)");

            free(pl2);
            free(filename_stackplot);
          }

          free(pl1);

          /* compute centroid structure */
          compute_centroid(vc, opt->ligandMotif, opt->verbose, output);

          /* compute MEA structure */
          if (opt->MEA) {
            compute_MEA(vc,
                        opt->MEAgamma,
                        opt->ligandMotif,
                        opt->verbose,
                        output);
          }
        } else {
          char *msg = vrna_strdup_printf(" free energy of ensemble = %6.2f kcal/mol", energy);
          print_structure(output, NULL, msg);
          free(msg);
        }

        /* finalize enemble properties for this sequence input */
        if (output) {
          char *msg = NULL;
          if (opt->md.compute_bpp) {
            msg = vrna_strdup_printf(" frequency of mfe structure in ensemble %g"
                                     "; ensemble diversity %-6.2f",
                                     vrna_pr_structure(vc, mfe_structure),
                                     vrna_mean_bp_distance(vc));
          } else {
            msg = vrna_strdup_printf(" frequency of mfe structure in ensemble %g;",
                                     vrna_pr_structure(vc, mfe_structure));
          }

          print_structure(output, NULL, msg);
          free(msg);
        }
      }

      free(pf_struc);
    }

    if (output)
      (void)fflush(output);

    if (opt->tofile && output) {
      fclose(output);
      output = NULL;
    }

    free(v_file_name);

    /* clean up */
    vrna_fold_compound_free(vc);
    free(rec_id);
    free(rec_sequence);
    free(orig_sequence);
    free(mfe_structure);

    /* free the rest of current dataset */
    if (rec_rest) {
      for (int i = 0; rec_rest[i]; i++)
        free(rec_rest[i]);
      free(rec_rest);
    }

    rec_id    = rec_sequence = mfe_structure = NULL;
    rec_rest  = NULL;

    if (opt->shape || (opt->constraint_file && (!opt->constraint_batch)))
      break;

    free(SEQ_ID);

    /* print user help for the next round if we get input from tty */
    if (istty_in && istty_out) {
      if (fold_constrained) {
        vrna_message_constraint_options_all();
        vrna_message_input_seq(
          "Input sequence (upper or lower case) followed by structure constraint");
      } else {
        vrna_message_input_seq_simple();
      }
    }
  }
}


static void
apply_constraints(vrna_fold_compound_t  *fc,
                  const char            *constraints_file,
                  const char            **rec_rest,
                  int                   maybe_multiline,
                  int                   enforceConstraints,
                  int                   canonicalBPonly)
{
  if (constraints_file) {
    /** [Adding hard constraints from file] */
    vrna_constraints_add(fc, constraints_file, VRNA_OPTION_DEFAULT);
    /** [Adding hard constraints from file] */
  } else {
    char          *cstruc   = NULL;
    unsigned int  length    = fc->length;
    unsigned int  coptions  = (maybe_multiline) ? VRNA_OPTION_MULTILINE : 0;
    cstruc = vrna_extract_record_rest_structure((const char **)rec_rest, 0, coptions);
    unsigned int  cl = (cstruc) ? strlen(cstruc) : 0;

    if (cl == 0)
      vrna_message_warning("structure constraint is missing");
    else if (cl < length)
      vrna_message_warning("structure constraint is shorter than sequence");
    else if (cl > length)
      vrna_message_error("structure constraint is too long");

    if (cstruc) {
      /** [Adding hard constraints from pseudo dot-bracket] */
      unsigned int constraint_options = VRNA_CONSTRAINT_DB_DEFAULT;

      if (enforceConstraints)
        constraint_options |= VRNA_CONSTRAINT_DB_ENFORCE_BP;

      if (canonicalBPonly)
        constraint_options |= VRNA_CONSTRAINT_DB_CANONICAL_BP;

      vrna_constraints_add(fc, (const char *)cstruc, constraint_options);
      /** [Adding hard constraints from pseudo dot-bracket] */

      free(cstruc);
    }
  }
}


static void
compute_MEA(vrna_fold_compound_t  *fc,
            double                MEAgamma,
            const char            *ligandMotif,
            int                   verbose,
            FILE                  *output)
{
  char  *structure;
  float mea, mea_en;
  /*  this is a hack since vrna_plist_from_probs() always resolves g-quad pairs,
   *  while MEA_seq() still expects unresolved gquads */
  int   gq = fc->exp_params->model_details.gquad;

  /* we need to create a string as long as the sequence for the MEA implementation :( */
  structure = strdup(fc->sequence);

  fc->exp_params->model_details.gquad = 0;
  plist *pl = vrna_plist_from_probs(fc, 1e-4 / (1 + MEAgamma));
  fc->exp_params->model_details.gquad = gq;

  if (gq)
    mea = MEA_seq(pl, fc->sequence, structure, MEAgamma, fc->exp_params);
  else
    mea = MEA(pl, structure, MEAgamma);

  mea_en = vrna_eval_structure(fc, (const char *)structure);
  if (output) {
    char *msg = vrna_strdup_printf(" {%6.2f MEA=%.2f}", mea_en, mea);
    print_structure(output, structure, msg);
    free(msg);
  }

  if ((ligandMotif) && (verbose))
    print_ligand_motifs(fc, structure, "MEA", output);

  if ((fc->domains_up) && (verbose))
    print_ud_motifs(fc, structure, "MEA", output);

  free(pl);
  free(structure);
}


static void
compute_centroid(vrna_fold_compound_t *fc,
                 const char           *ligandMotif,
                 int                  verbose,
                 FILE                 *output)
{
  char    *cent;
  double  cent_en, dist;

  cent    = vrna_centroid(fc, &dist);
  cent_en = vrna_eval_structure(fc, (const char *)cent);

  if (output) {
    char *msg = vrna_strdup_printf(" {%6.2f d=%.2f}", cent_en, dist);
    print_structure(output, cent, msg);
    free(msg);
  }

  if ((ligandMotif) && (verbose))
    print_ligand_motifs(fc, cent, "centroid", output);

  if ((fc->domains_up) && (verbose))
    print_ud_motifs(fc, cent, "centroid", output);

  free(cent);
}


static void
add_ligand_motif(vrna_fold_compound_t *vc,
                 char                 *motifstring,
                 int                  verbose,
                 unsigned int         options)
{
  int   r, l, error;
  char  *seq, *str, *ptr;
  float energy;

  l   = strlen(motifstring);
  seq = vrna_alloc(sizeof(char) * (l + 1));
  str = vrna_alloc(sizeof(char) * (l + 1));

  error = 1;

  if (motifstring) {
    error = 0;
    /* parse sequence */
    for (r = 0, ptr = motifstring; *ptr != '\0'; ptr++) {
      if (*ptr == ',')
        break;

      seq[r++] = toupper(*ptr);
    }
    seq[r]  = '\0';
    seq     = vrna_realloc(seq, sizeof(char) * (strlen(seq) + 1));

    for (ptr++, r = 0; *ptr != '\0'; ptr++) {
      if (*ptr == ',')
        break;

      str[r++] = *ptr;
    }
    str[r]  = '\0';
    str     = vrna_realloc(str, sizeof(char) * (strlen(seq) + 1));

    ptr++;
    if (!(sscanf(ptr, "%f", &energy) == 1)) {
      vrna_message_warning("Energy contribution in ligand motif missing!");
      error = 1;
    }

    if (strlen(seq) != strlen(str)) {
      vrna_message_warning("Sequence and structure length in ligand motif have unequal lengths!");
      error = 1;
    }

    if (strlen(seq) == 0) {
      vrna_message_warning("Sequence length in ligand motif is zero!");
      error = 1;
    }

    if (!error && verbose)
      vrna_message_info(stderr, "Read ligand motif: %s, %s, %f", seq, str, energy);
  }

  if (error || (!vrna_sc_add_hi_motif(vc, seq, str, energy, options)))
    vrna_message_warning("Malformatted ligand motif! Skipping stabilizing motif.");

  free(seq);
  free(str);
}


static char *
annotate_ligand_motif(vrna_fold_compound_t  *vc,
                      const char            *structure)
{
  char            *annote;
  vrna_sc_motif_t *motifs, *m_ptr;

  annote  = NULL;
  motifs  = vrna_sc_ligand_detect_motifs(vc, structure);

  if (motifs) {
    for (m_ptr = motifs; m_ptr->i; m_ptr++) {
      char *tmp_string, *annotation;
      annotation  = NULL;
      tmp_string  = annote;

      if (m_ptr->i != m_ptr->k) {
        annotation = vrna_strdup_printf(" %d %d %d %d 1. 0 0 BFmark",
                                        m_ptr->i,
                                        m_ptr->j,
                                        m_ptr->k,
                                        m_ptr->l);
      } else {
        annotation = vrna_strdup_printf(" %d %d 1. 0 0 Fomark",
                                        m_ptr->i,
                                        m_ptr->j);
      }

      if (tmp_string)
        annote = vrna_strdup_printf("%s %s", tmp_string, annotation);
      else
        annote = strdup(annotation);

      free(tmp_string);
      free(annotation);
    }
  }

  free(motifs);

  return annote;
}


static void
print_ligand_motifs(vrna_fold_compound_t  *vc,
                    const char            *structure,
                    const char            *structure_name,
                    FILE                  *output)
{
  vrna_sc_motif_t *motifs, *m_ptr;

  motifs = vrna_sc_ligand_detect_motifs(vc, structure);

  if (motifs) {
    for (m_ptr = motifs; m_ptr->i; m_ptr++) {
      if (m_ptr->i != m_ptr->k) {
        vrna_message_info(output,
                          "specified motif detected in %s structure: [%d:%d] & [%d:%d]",
                          structure_name,
                          m_ptr->i,
                          m_ptr->k,
                          m_ptr->l,
                          m_ptr->j);
      } else {
        vrna_message_info(output,
                          "specified motif detected in %s structure: [%d:%d]",
                          structure_name,
                          m_ptr->i,
                          m_ptr->j);
      }
    }
  }

  free(motifs);
}


static char *
annotate_ud_motif(vrna_fold_compound_t  *vc,
                  const char            *structure)
{
  int   m, i, size;
  char  *annote;

  m       = 0;
  annote  = NULL;

  if (vc->domains_up) {
    vrna_ud_motif_t *motifs = vrna_ud_detect_motifs(vc, structure);

    if (motifs) {
      while (motifs[m].start != 0) {
        char  *tmp_string = annote;
        i     = motifs[m].start;
        size  = vc->domains_up->motif_size[motifs[m].number];
        char  *annotation;

        annotation = vrna_strdup_printf(" %d %d 12 0.4 0.65 0.95 omark", i, i + size - 1);

        if (tmp_string)
          annote = vrna_strdup_printf("%s %s", tmp_string, annotation);
        else
          annote = strdup(annotation);

        free(tmp_string);
        free(annotation);
        m++;
      }
    }

    free(motifs);
  }

  return annote;
}


static void
print_ud_motifs(vrna_fold_compound_t  *vc,
                const char            *structure,
                const char            *structure_name,
                FILE                  *output)
{
  int m, i, size;

  m = 0;

  if (vc->domains_up) {
    vrna_ud_motif_t *motifs = vrna_ud_detect_motifs(vc, structure);

    if (motifs) {
      while (motifs[m].start != 0) {
        i     = motifs[m].start;
        size  = vc->domains_up->motif_size[motifs[m].number];

        vrna_message_info(output,
                          "ud motif %d detected in %s structure: [%d:%d]",
                          motifs[m].number,
                          structure_name,
                          i,
                          i + size - 1);

        m++;
      }
    }

    free(motifs);
  }
}


static void
add_ligand_motifs_dot(vrna_fold_compound_t  *fc,
                      vrna_ep_t             **prob_list,
                      vrna_ep_t             **mfe_list,
                      const char            *structure)
{
  vrna_sc_motif_t *motifs;

  /* append motif positions to the plists of base pair probabilities */
  motifs = vrna_sc_ligand_get_all_motifs(fc);
  if (motifs) {
    add_ligand_motifs_to_list(prob_list, motifs);
    free(motifs);
  }

  /* now scan for the motif in MFE structure again */
  motifs = vrna_sc_ligand_detect_motifs(fc, structure);
  if (motifs) {
    add_ligand_motifs_to_list(mfe_list, motifs);
    free(motifs);
  }
}


static void
add_ligand_motifs_to_list(vrna_ep_t       **list,
                          vrna_sc_motif_t *motifs)
{
  unsigned int    cnt, add, size;
  vrna_ep_t       *ptr;
  vrna_sc_motif_t *m_ptr;

  cnt = 0;
  add = 10;

  /* get current size of list */
  for (size = 0, ptr = (*list); ptr->i; size++, ptr++) ;

  /* increase length of list */
  (*list) = vrna_realloc((*list), sizeof(vrna_ep_t) * (size + add + 1));

  for (m_ptr = motifs; m_ptr->i; m_ptr++) {
    if (m_ptr->i == m_ptr->k) {
      /* hairpin motif */
      (*list)[size + cnt].i     = m_ptr->i;
      (*list)[size + cnt].j     = m_ptr->j;
      (*list)[size + cnt].p     = 0.95 * 0.95;
      (*list)[size + cnt].type  = VRNA_PLIST_TYPE_H_MOTIF;
      cnt++;
      if (cnt == add) {
        add += 10;
        /* increase length of (*prob_list) */
        (*list) = vrna_realloc((*list), sizeof(vrna_ep_t) * (size + add + 1));
      }
    } else {
      /* interior loop motif */
      (*list)[size + cnt].i     = m_ptr->i;
      (*list)[size + cnt].j     = m_ptr->j;
      (*list)[size + cnt].p     = 0.95 * 0.95;
      (*list)[size + cnt].type  = VRNA_PLIST_TYPE_I_MOTIF;
      cnt++;
      (*list)[size + cnt].i     = m_ptr->k;
      (*list)[size + cnt].j     = m_ptr->l;
      (*list)[size + cnt].p     = 0.95 * 0.95;
      (*list)[size + cnt].type  = VRNA_PLIST_TYPE_I_MOTIF;
      cnt++;
      if (cnt == add) {
        add += 10;
        /* increase length of (*prob_list) */
        (*list) = vrna_realloc((*list), sizeof(vrna_ep_t) * (size + add + 1));
      }
    }
  }

  /* resize pl1 to actual needs */
  (*list)               = vrna_realloc((*list), sizeof(vrna_ep_t) * (size + cnt + 1));
  (*list)[size + cnt].i = 0;
  (*list)[size + cnt].j = 0;
}
