<?php

/**
 * @file
 * Date query type plugin for the Search API adapter.
 */

/**
 * Plugin for "date" query types.
 */
class SearchApiFacetapiDate extends SearchApiFacetapiTerm implements FacetapiQueryTypeInterface {

  /**
   * Loads the include file containing the date API functions.
   */
  public function __construct(FacetapiAdapter $adapter, array $facet) {
    module_load_include('date.inc', 'facetapi');
    parent::__construct($adapter, $facet);
  }

  /**
   * Returns the query type associated with the plugin.
   *
   * @return string
   *   The query type.
   */
  static public function getType() {
    return 'date';
  }

  /**
   * Adds the filter to the query object.
   *
   * @param $query
   *   An object containing the query in the backend's native API.
   */
  public function execute($query) {
    // Return terms for this facet.
    $this->adapter->addFacet($this->facet, $query);

    $settings = $this->adapter->getFacet($this->facet)->getSettings()->settings;

    // First check if the facet is enabled for this search.
    $default_true = isset($settings['default_true']) ? $settings['default_true'] : TRUE;
    $facet_search_ids = isset($settings['facet_search_ids']) ? $settings['facet_search_ids'] : array();
    if ($default_true != empty($facet_search_ids[$query->getOption('search id')])) {
      // Facet is not enabled for this search ID.
      return;
    }

    // Change limit to "unlimited" (-1).
    $options = &$query->getOptions();
    if (!empty($options['search_api_facets'][$this->facet['name']])) {
      $options['search_api_facets'][$this->facet['name']]['limit'] = -1;
    }

    if ($active_items = $this->adapter->getActiveItems($this->facet)) {
      $field = $this->facet['field'];
      $operator = 'OR';
      if ($settings['operator'] !== FACETAPI_OPERATOR_OR) {
        $operator = 'AND';
        // If the operator is AND, we just need to apply the lowest-level
        // filter(s) to make this work correctly. For single-valued fields, this
        // will always just be the last value, so just use that to improve
        // performance for that case.
        $fields = $query->getIndex()->getFields();
        if (isset($fields[$field]['type'])
            && !search_api_is_list_type($fields[$field]['type'])) {
          $active_items = array(end($active_items));
        }
      }
      $date_query = $query->createFilter($operator, array("facet:$field"));
      foreach($active_items as $active_item) {
        $filter = $this->createRangeFilter($active_item['value']);
        if ($filter) {
          $this->addFacetFilter($date_query, $field, $filter, $query);
        }
      }
      $query->filter($date_query);
    }
  }

  /**
   * Rewrites the handler-specific date range syntax to the normal facet syntax.
   *
   * @param string $value
   *   The user-facing facet value.
   *
   * @return string|null
   *   A facet to add as a filter, in the format used internally in this module.
   *   Or NULL if the raw facet in $value is not valid.
   */
  protected function createRangeFilter($value) {
    // Ignore any filters passed directly from the server (range or missing).
    if (!$value || $value == '!' || (!ctype_digit($value[0]) && preg_match('/^[\[(][^ ]+ TO [^ ]+[\])]$/', $value))) {
      return $value ? $value : NULL;
    }

    // Parse into date parts.
    $parts = $this->parseRangeFilter($value);

    // Return NULL if the date parts are invalid or none were found.
    if (empty($parts)) {
      return NULL;
    }

    $date = new DateTime();
    switch (count($parts)) {
      case 1:
        $date->setDate($parts[0], 1, 1);
        $date->setTime(0, 0, 0);
        $lower = $date->format('U');
        $date->setDate($parts[0] + 1, 1, 1);
        $date->setTime(0, 0, -1);
        $upper = $date->format('U');
        break;

      case 2:
        // Luckily, $month = 13 is treated as January of next year. (The same
        // goes for all other parameters.) We use the inverse trick for the
        // seconds of the upper bound, since that's inclusive and we want to
        // stop at a second before the next segment starts.
        $date->setDate($parts[0], $parts[1], 1);
        $date->setTime(0, 0, 0);
        $lower = $date->format('U');
        $date->setDate($parts[0], $parts[1] + 1, 1);
        $date->setTime(0, 0, -1);
        $upper = $date->format('U');
        break;

      case 3:
        $date->setDate($parts[0], $parts[1], $parts[2]);
        $date->setTime(0, 0, 0);
        $lower = $date->format('U');
        $date->setDate($parts[0], $parts[1], $parts[2] + 1);
        $date->setTime(0, 0, -1);
        $upper = $date->format('U');
        break;

      case 4:
        $date->setDate($parts[0], $parts[1], $parts[2]);
        $date->setTime($parts[3], 0, 0);
        $lower = $date->format('U');
        $date->setTime($parts[3] + 1, 0, -1);
        $upper = $date->format('U');
        break;

      case 5:
        $date->setDate($parts[0], $parts[1], $parts[2]);
        $date->setTime($parts[3], $parts[4], 0);
        $lower = $date->format('U');
        $date->setTime($parts[3], $parts[4] + 1, -1);
        $upper = $date->format('U');
        break;

      case 6:
        $date->setDate($parts[0], $parts[1], $parts[2]);
        $date->setTime($parts[3], $parts[4], $parts[5]);
        return $date->format('U');

      default:
        return $value;
    }

    return "[$lower TO $upper]";
  }

  /**
   * Parses the date range filter value into parts.
   *
   * @param string $value
   *   The user-facing facet value.
   *
   * @return int[]|null
   *   An array of date parts, or NULL if an invalid value was provided.
   */
  protected static function parseRangeFilter($value) {
    $parts = explode('-', $value);

    foreach ($parts as $i => $part) {
      // Invalidate if part is not an integer.
      if ($part === '' || !is_numeric($part) || intval($part) != $part) {
        return NULL;
      }
      $parts[$i] = (int) $part;
      // Depending on the position, negative numbers or 0 are invalid.
      switch ($i) {
        case 0:
          // Years can contain anything – negative values are unlikely, but
          // technically possible.
          break;
        case 1:
        case 2:
          // Days and months have to be positive.
          if ($part <= 0) {
            return NULL;
          }
          break;
        default:
          // All others can be 0, but not negative.
          if ($part < 0) {
            return NULL;
          }
      }
    }

    return $parts;
  }

  /**
   * Replacement callback for replacing ISO dates with timestamps.
   *
   * Not used anymore, but kept for backwards compatibility with potential
   * subclasses.
   */
  public function replaceDateString($matches) {
    return strtotime($matches[0]);
  }

  /**
   * Initializes the facet's build array.
   *
   * @return array
   *   The initialized render array.
   */
  public function build() {
    $facet = $this->adapter->getFacet($this->facet);
    $search_ids = backdrop_static('search_api_facetapi_active_facets', array());
    $facet_key = $facet['name'] . '@' . $this->adapter->getSearcher();
    if (empty($search_ids[$facet_key]) || !search_api_current_search($search_ids[$facet_key])) {
      return array();
    }
    $search_id = $search_ids[$facet_key];
    $build = array();
    $search = search_api_current_search($search_id);
    $results = $search[1];
    // Gets total number of documents matched in search.
    $total = $results['result count'];

    // Executes query, iterates over results.
    if (isset($results['search_api_facets']) && isset($results['search_api_facets'][$this->facet['name']])) {
      $values = $results['search_api_facets'][$this->facet['name']];
      $mincount = $facet->getSettings()->settings['facet_mincount'];
      foreach ($values as $value) {
        if ($value['count'] >= $mincount) {
          $filter = $value['filter'];
          // We only process single values further. The "missing" filter and
          // range filters will be passed on unchanged.
          if ($filter == '!') {
            $build[$filter]['#count'] = $value['count'];
          }
          elseif ($filter[0] == '"') {
            $filter = substr($value['filter'], 1, -1);
            if ($filter) {
              $raw_values[$filter] = $value['count'];
            }
          }
          else {
            $build[$filter]['#count'] = $value['count'];
          }
        }
      }
    }

    $settings = $facet->getSettings()->settings;

    // Get the finest level of detail we're allowed to drill down to.
    $max_granularity = FACETAPI_DATE_MINUTE;
    if (isset($settings['date_granularity'])) {
      $max_granularity = $settings['date_granularity'];
    }

    // Get the coarsest level of detail we're allowed to start at.
    $min_granularity = FACETAPI_DATE_YEAR;
    if (isset($settings['date_granularity_min'])) {
      $min_granularity = $settings['date_granularity_min'];
    }

    // Gets active facets, starts building hierarchy.
    $parent = $granularity = NULL;
    $active_items = $this->adapter->getActiveItems($this->facet);
    foreach ($active_items as $value => $item) {
      // If the item is active, the count is the result set count.
      $build[$value] = array('#count' => $total);

      // Gets next "gap" increment. Ignore any filters passed directly from the
      // server (range or missing). We always create filters starting with a
      // year.
      $value = "$value";
      if (!$value || !ctype_digit($value[0])) {
        continue;
      }

      $granularity = search_api_facetapi_date_get_granularity($value);
      if (!$granularity) {
        continue;
      }
      $granularity = facetapi_get_next_date_gap($granularity, $max_granularity);

      // If there is a previous item, there is a parent, uses a reference so the
      // arrays are populated when they are updated.
      if (NULL !== $parent) {
        $build[$parent]['#item_children'][$value] = &$build[$value];
        $build[$value]['#item_parents'][$parent] = $parent;
      }
    }

    if (empty($raw_values)) {
      return $build;
    }
    ksort($raw_values);

    // Mind the gap! Calculates gap from min and max timestamps.
    $timestamps = array_keys($raw_values);
    if (NULL === $parent) {
      if (count($raw_values) > 1) {
        $granularity = facetapi_get_timestamp_gap(min($timestamps), max($timestamps), $max_granularity);
        // Array of numbers used to determine whether the next gap is smaller than
        // the minimum gap allowed in the drilldown.
        $gap_numbers = array(
          FACETAPI_DATE_YEAR => 6,
          FACETAPI_DATE_MONTH => 5,
          FACETAPI_DATE_DAY => 4,
          FACETAPI_DATE_HOUR => 3,
          FACETAPI_DATE_MINUTE => 2,
          FACETAPI_DATE_SECOND => 1,
        );
        // Gets gap numbers for both the gap, minimum and maximum gap, checks if
        // the gap is within the limit set by the $granularity parameters.
        if ($gap_numbers[$granularity] < $gap_numbers[$max_granularity]) {
          $granularity = $max_granularity;
        }
        if ($gap_numbers[$granularity] > $gap_numbers[$min_granularity]) {
          $granularity = $min_granularity;
        }
      }
      else {
        $granularity = $max_granularity;
      }
    }

    // Groups dates by the range they belong to, builds the $build array with
    // the facet counts and formatted range values.
    $format = search_api_facetapi_date_get_granularity_format($granularity);
    foreach ($raw_values as $value => $count) {
      $new_value = date($format, $value);
      if (!isset($build[$new_value])) {
        $build[$new_value] = array('#count' => $count);
      }
      // Active items already have their value set because it's the current
      // result count.
      elseif (!isset($active_items[$new_value])) {
        $build[$new_value]['#count'] += $count;
      }

      // Adds parent information if not already set.
      if (NULL !== $parent && $parent != $new_value) {
        $build[$parent]['#item_children'][$new_value] = &$build[$new_value];
        $build[$new_value]['#item_parents'][$parent] = $parent;
      }
    }

    return $build;
  }

}
