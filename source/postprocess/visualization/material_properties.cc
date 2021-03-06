/*
  Copyright (C) 2011 - 2017 by the authors of the ASPECT code.

  This file is part of ASPECT.

  ASPECT is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.

  ASPECT is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with ASPECT; see the file doc/COPYING.  If not see
  <http://www.gnu.org/licenses/>.
*/


#include <aspect/postprocess/visualization/material_properties.h>
#include <aspect/utilities.h>

#include <algorithm>


namespace aspect
{
  namespace Postprocess
  {
    namespace VisualizationPostprocessors
    {
      template <int dim>
      MaterialProperties<dim>::
      MaterialProperties ()
        :
        DataPostprocessor<dim> ()
      {}

      template <int dim>
      std::vector<std::string>
      MaterialProperties<dim>::
      get_names () const
      {
        std::vector<std::string> solution_names;

        for (unsigned int i=0; i<property_names.size(); ++i)
          if (property_names[i] == "reaction terms")
            {
              for (unsigned int c=0; c<this->n_compositional_fields(); ++c)
                solution_names.push_back (this->introspection().name_for_compositional_index(c) + "_change");
            }
          else
            {
              solution_names.push_back(property_names[i]);
              std::replace(solution_names.back().begin(),solution_names.back().end(),' ', '_');
            }

        return solution_names;
      }

      template <int dim>
      std::vector<DataComponentInterpretation::DataComponentInterpretation>
      MaterialProperties<dim>::
      get_data_component_interpretation () const
      {
        std::vector<DataComponentInterpretation::DataComponentInterpretation> interpretation;
        for (unsigned int i=0; i<property_names.size(); ++i)
          {
            if (property_names[i] == "reaction terms")
              {
                for (unsigned int c=0; c<this->n_compositional_fields(); ++c)
                  interpretation.push_back (DataComponentInterpretation::component_is_scalar);
              }
            else
              interpretation.push_back (DataComponentInterpretation::component_is_scalar);
          }

        return interpretation;
      }

      template <int dim>
      UpdateFlags
      MaterialProperties<dim>::
      get_needed_update_flags () const
      {
        return update_gradients | update_values  | update_q_points;
      }

      template <int dim>
      void
      MaterialProperties<dim>::
      evaluate_vector_field(const DataPostprocessorInputs::Vector<dim> &input_data,
                            std::vector<Vector<double> > &computed_quantities) const
      {
        const unsigned int n_quadrature_points = input_data.solution_values.size();
        Assert (computed_quantities.size() == n_quadrature_points,    ExcInternalError());
        Assert (input_data.solution_values[0].size() == this->introspection().n_components,           ExcInternalError());

        MaterialModel::MaterialModelInputs<dim> in(n_quadrature_points,
                                                   this->n_compositional_fields());
        MaterialModel::MaterialModelOutputs<dim> out(n_quadrature_points,
                                                     this->n_compositional_fields());

        in.position = input_data.evaluation_points;
        for (unsigned int q=0; q<n_quadrature_points; ++q)
          {
            Tensor<2,dim> grad_u;
            for (unsigned int d=0; d<dim; ++d)
              {
                grad_u[d] = input_data.solution_gradients[q][d];
                in.velocity[q][d] = input_data.solution_values[q][this->introspection().component_indices.velocities[d]];
                in.pressure_gradient[q][d] = input_data.solution_gradients[q][this->introspection().component_indices.pressure][d];
              }

            in.strain_rate[q] = symmetrize (grad_u);

            in.pressure[q] = input_data.solution_values[q][this->introspection().component_indices.pressure];
            in.temperature[q] = input_data.solution_values[q][this->introspection().component_indices.temperature];

            for (unsigned int c=0; c<this->n_compositional_fields(); ++c)
              in.composition[q][c] = input_data.solution_values[q][this->introspection().component_indices.compositional_fields[c]];
          }

        this->get_material_model().evaluate(in, out);

        for (unsigned int q=0; q<n_quadrature_points; ++q)
          {
            unsigned output_index = 0;
            for (unsigned int i=0; i<property_names.size(); ++i, ++output_index)
              {
                if (property_names[i] == "viscosity")
                  computed_quantities[q][output_index] = out.viscosities[q];

                else if (property_names[i] == "density")
                  computed_quantities[q][output_index] = out.densities[q];

                else if (property_names[i] == "thermal expansivity")
                  computed_quantities[q][output_index] = out.thermal_expansion_coefficients[q];

                else if (property_names[i] == "specific heat")
                  computed_quantities[q][output_index] = out.specific_heat[q];

                else if (property_names[i] == "thermal conductivity")
                  computed_quantities[q][output_index] = out.thermal_conductivities[q];

                else if (property_names[i] == "thermal diffusivity")
                  computed_quantities[q][output_index] = out.thermal_conductivities[q]/(out.densities[q]*out.specific_heat[q]);

                else if (property_names[i] == "compressibility")
                  computed_quantities[q][output_index] = out.compressibilities[q];

                else if (property_names[i] == "entropy derivative pressure")
                  computed_quantities[q][output_index] = out.entropy_derivative_pressure[q];

                else if (property_names[i] == "entropy derivative temperature")
                  computed_quantities[q][output_index] = out.entropy_derivative_temperature[q];

                else if (property_names[i] == "reaction terms")
                  {
                    for (unsigned int k=0; k<this->n_compositional_fields(); ++k, ++output_index)
                      {
                        computed_quantities[q][output_index] = out.reaction_terms[q][k];
                      }
                    --output_index;
                  }
                else
                  AssertThrow(false,
                              ExcMessage("Material property not implemented for this postprocessor."));
              }
          }
      }

      template <int dim>
      void
      MaterialProperties<dim>::declare_parameters (ParameterHandler &prm)
      {
        prm.enter_subsection("Postprocess");
        {
          prm.enter_subsection("Visualization");
          {
            prm.enter_subsection("Material properties");
            {
              const std::string pattern_of_names
                = "viscosity|density|thermal expansivity|specific heat|"
                  "thermal conductivity|thermal diffusivity|compressibility|"
                  "entropy derivative temperature|entropy derivative pressure|reaction terms";

              prm.declare_entry("List of material properties",
                                "density,thermal expansivity,specific heat,viscosity",
                                Patterns::MultipleSelection(pattern_of_names),
                                "A comma separated list of material properties that should be "
                                "written whenever writing graphical output. By default, the "
                                "material properties will always contain the density, thermal "
                                "expansivity, specific heat and viscosity. "
                                "The following material properties are available:\n\n"
                                +
                                pattern_of_names);
            }
            prm.leave_subsection();
          }
          prm.leave_subsection();
        }
        prm.leave_subsection();
      }


      template <int dim>
      void
      MaterialProperties<dim>::parse_parameters (ParameterHandler &prm)
      {
        prm.enter_subsection("Postprocess");
        {
          prm.enter_subsection("Visualization");
          {
            // Find out which variables are registered separately
            std::vector<std::string> variable_names;
            variable_names = Utilities::split_string_list(prm.get("List of output variables"));

            prm.enter_subsection("Material properties");
            {
              // Get property names and compare against variable names
              property_names = Utilities::split_string_list(prm.get ("List of material properties"));
              AssertThrow(Utilities::has_unique_entries(property_names),
                          ExcMessage("The list of strings for the parameter "
                                     "'Postprocess/Visualization/Material properties/List of material properties' "
                                     "contains entries more than once. This is not allowed. "
                                     "Please check your parameter file."));

              for (std::vector<std::string>::const_iterator p = variable_names.begin();
                   p != variable_names.end(); ++p)
                AssertThrow((std::find(property_names.begin(),property_names.end(),*p)) == property_names.end(),
                            ExcMessage("Visualization postprocessor "
                                       + *p
                                       + " is listed separately and in the list of material properties."));
            }
            prm.leave_subsection();
          }
          prm.leave_subsection();
        }
        prm.leave_subsection();
      }
    }
  }
}


// explicit instantiations
namespace aspect
{
  namespace Postprocess
  {
    namespace VisualizationPostprocessors
    {
      ASPECT_REGISTER_VISUALIZATION_POSTPROCESSOR(MaterialProperties,
                                                  "material properties",
                                                  "A visualization output object that generates output "
                                                  "for the material properties given by the material model."
                                                  "There are a number of other visualization postprocessors "
                                                  "that offer to write individual material properties. However, "
                                                  "they all individually have to evaluate the material model. "
                                                  "This is inefficient if one wants to output more than just "
                                                  "one or two of the fields provided by the material model. "
                                                  "The current postprocessor allows to output a (potentially "
                                                  "large) subset of all of the information provided by "
                                                  "material models at once, with just a single material model "
                                                  "evaluation per output point.")
    }
  }
}
